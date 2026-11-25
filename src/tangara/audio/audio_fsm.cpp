/*
 * Copyright 2023 jacqueline <me@jacqueline.id.au>
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "audio/audio_fsm.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <sstream>
#include <variant>

#include "audio/audio_source.hpp"
#include "audio/sine_source.hpp"
#include "cppbor.h"
#include "cppbor_parse.h"
#include "drivers/pcm_buffer.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/projdefs.h"
#include "freertos/timers.h"
#include "tinyfsm.hpp"

#include "audio/audio_debug.hpp"
#include "audio/audio_decoder.hpp"
#include "audio/audio_events.hpp"
#include "audio/audio_sink.hpp"
#include "audio/bt_audio_output.hpp"
#include "audio/fatfs_stream_factory.hpp"
#include "audio/i2s_audio_output.hpp"
#include "audio/stream_cues.hpp"
#include "audio/track_queue.hpp"
#include "database/future_fetcher.hpp"
#include "database/track.hpp"
#include "drivers/bluetooth.hpp"
#include "drivers/display.hpp"
#include "drivers/bluetooth_types.hpp"
#include "drivers/i2s_dac.hpp"
#include "drivers/nvs.hpp"
#include "drivers/storage.hpp"
#include "drivers/wm8523.hpp"
#include "events/event_queue.hpp"
#include "sample.hpp"
#include "system_fsm/service_locator.hpp"
#include "system_fsm/system_events.hpp"
#include "tts/player.hpp"

namespace audio {

[[maybe_unused]] static const char kTag[] = "audio_fsm";

static const char kQueueKey[] = "audio:queue";
static const char kCurrentFileKey[] = "audio:current";

// How long to wait after the last volume (or volume setting) change before
// writing it to flash. Without this, the volume was only saved when the
// device went idle, so turning it off or resetting it lost the change.
static const TickType_t kSettingsWriteDelay = pdMS_TO_TICKS(2000);
static TimerHandle_t sSettingsWriteTimer = nullptr;
static auto schedulePrefetch() -> void;

// The queue's track list is written to the SD card as it is built, but the
// place in it (queue position, track position, repeat and shuffle) used to be
// saved only when the device went idle or the card was unmounted. A reboot,
// crash or flat battery mid-play lost it. Now the place is also saved:
//  - a few seconds after the current track changes (long enough that rapid
//    skipping costs one write, and that a track which crashes the decoder
//    straight away isn't saved as the one to resume),
//  - every kSavePlaceInterval seconds of playback,
//  - shortly after pausing, and after the queue itself changes.
// Each save is two small database puts (plus the queue's offset cache when
// the queue has grown), done on a background worker, so the audio path never
// waits on it.
static constexpr uint32_t kSavePlaceInterval = 30;
static constexpr uint32_t kTrackChangeSaveDelayMs = 3000;
static constexpr uint32_t kPauseSaveDelayMs = 500;
static constexpr uint32_t kQueueChangeSaveDelayMs = 3000;
static TimerHandle_t sSaveTimer = nullptr;
// False until the saved queue has been restored, so that nothing overwrites
// the saved place before it has been read back.
static std::atomic<bool> sCanSave{false};
// Saves run on any of the background workers; keep them in order.
static std::mutex sSaveMutex;
// The place most recently saved. Only touched on the audio task.
static std::string sLastSavedUri;
static uint32_t sLastSavedPosition = 0;

std::shared_ptr<system_fsm::ServiceLocator> AudioState::sServices;

std::shared_ptr<FatfsStreamFactory> AudioState::sStreamFactory;

std::unique_ptr<Decoder> AudioState::sDecoder;
std::shared_ptr<SampleProcessor> AudioState::sSampleProcessor;

std::shared_ptr<IAudioOutput> AudioState::sOutput;
std::shared_ptr<I2SAudioOutput> AudioState::sI2SOutput;
std::shared_ptr<BluetoothAudioOutput> AudioState::sBtOutput;

// CPU processing is generally not the bottleneck for audio; reading from the SD card is.
// Therefore these buffers can be relatively small, whereas ReadaheadSource's buffer
// is as big as possible.
constexpr size_t kTrackDrainLatencySamples = drivers::kI2SBufferLengthFrames * 2 * 32;
constexpr size_t kSystemDrainLatencySamples = drivers::kI2SBufferLengthFrames * 2 * 8;

std::unique_ptr<drivers::OutputBuffers> AudioState::sDrainBuffers;
std::optional<IAudioOutput::Format> AudioState::sDrainFormat;

StreamCues AudioState::sStreamCues;

bool AudioState::sIsPaused = true;
bool AudioState::sIsTtsPlaying = false;

uint8_t AudioState::sUpdateCounter = 0;

auto AudioState::emitPlaybackUpdate(bool paused) -> void {
  std::optional<uint32_t> position;
  auto current = sStreamCues.current();
  if (current.first && sDrainFormat) {
    position = ((current.second +
                 (sDrainFormat->num_channels * sDrainFormat->sample_rate / 2)) /
                (sDrainFormat->num_channels * sDrainFormat->sample_rate)) +
               current.first->start_offset.value_or(0);
  }

  if (position) {
    maybeSavePlace(current.first->uri, *position);
  }

  // If we've got an elapsed duration and it's more than 5 minutes
  // increment a counter. Every 60 counts (ie, every minute) save the current
  // elapsed position
  if (position && *position > (5 * 60)) {
    sUpdateCounter++;
    if (sUpdateCounter >= 60) {
      sUpdateCounter = 0;
      updateSavedPosition(current.first->uri, *position);
    }
  }

  PlaybackUpdate event{
      .current_track = current.first,
      .track_position = position,
      .paused = paused,
  };

  events::System().Dispatch(event);
  events::Ui().Dispatch(event);
}

auto AudioState::loadQueue() -> void {
  sCanSave = false;
  sServices->bg_worker().Dispatch<void>([]() {
    auto db = sServices->database().lock();
    if (!db) {
      return;
    }

    // Open the queue file
    sServices->track_queue().open();

    // Restore the currently playing file before restoring the queue. This way,
    // we can fall back to restarting the queue's current track if there's any
    // issue restoring the current file.
    auto current = db->get(kCurrentFileKey);
    if (current) {
      // Again, ensure we don't boot-loop by trying to play a track that causes
      // a crash over and over again.
      db->put(kCurrentFileKey, "");
      auto [parsed, unused, err] = cppbor::parse(
          reinterpret_cast<uint8_t*>(current->data()), current->size());
      if (parsed->type() == cppbor::ARRAY) {
        std::string filename = parsed->asArray()->get(0)->asTstr()->value();
        uint32_t pos = parsed->asArray()->get(1)->asUint()->value();

        events::Audio().Dispatch(SetTrack{
            .new_track = filename,
            .seek_to_second = pos,
        });
      }
    }

    auto queue = db->get(kQueueKey);
    if (queue) {
      // Don't restore the same queue again. This ideally should do nothing,
      // but guards against bad edge cases where restoring the queue ends up
      // causing a crash.
      db->put(kQueueKey, "");
      sServices->track_queue().deserialise(*queue);
    }

    // The keys above were cleared, so write the restored place back once it
    // has had a few seconds to prove it doesn't crash. Otherwise turning the
    // device off again without pressing play would lose it.
    sCanSave = true;
    requestSave(kTrackChangeSaveDelayMs, true);
  });
}

static void save_timer_cb(TimerHandle_t) {
  events::Audio().Dispatch(internal::SavePlaybackState{});
}

auto AudioState::requestSave(uint32_t delay_ms, bool reset) -> void {
  // Created at boot, before anything can ask for a save.
  if (!sSaveTimer) {
    return;
  }
  if (!reset && xTimerIsTimerActive(sSaveTimer)) {
    return;
  }
  // Changing the period also (re)starts the timer.
  xTimerChangePeriod(sSaveTimer, pdMS_TO_TICKS(delay_ms), 0);
}

auto AudioState::maybeSavePlace(const std::string& uri, uint32_t position)
    -> void {
  if (uri != sLastSavedUri) {
    requestSave(kTrackChangeSaveDelayMs, false);
    return;
  }
  uint32_t moved = position > sLastSavedPosition
                       ? position - sLastSavedPosition
                       : sLastSavedPosition - position;
  // While paused this only catches seeks, since the position stands still.
  if (moved >= kSavePlaceInterval) {
    savePlaybackState();
  }
}

void AudioState::react(const internal::SavePlaybackState&) {
  savePlaybackState();
}

auto AudioState::savePlaybackState(std::function<void()> done) -> void {
  if (!sCanSave) {
    if (done) {
      done();
    }
    return;
  }

  // Work out the place here, on the audio task, which owns the stream cues.
  std::optional<std::pair<std::string, uint32_t>> place;
  auto current = sStreamCues.current();
  if (current.first && sDrainFormat) {
    uint32_t seconds = (current.second / (sDrainFormat->num_channels *
                                          sDrainFormat->sample_rate)) +
                       current.first->start_offset.value_or(0);
    place.emplace(current.first->uri, seconds);
    sLastSavedUri = current.first->uri;
    sLastSavedPosition = seconds;
  } else {
    sLastSavedUri.clear();
    sLastSavedPosition = 0;
  }

  sServices->bg_worker().Dispatch<void>([=]() {
    {
      std::lock_guard<std::mutex> lock{sSaveMutex};
      auto db = sServices->database().lock();
      if (sCanSave && db) {
        auto& queue = sServices->track_queue();
        if (queue.totalSize() <= queue.currentPosition()) {
          // Nothing is playing, so there's no place to come back to.
          db->put(kQueueKey, "");
          db->put(kCurrentFileKey, "");
        } else {
          db->put(kQueueKey, queue.serialise());
          if (place) {
            cppbor::Array current_track{
                cppbor::Tstr{place->first},
                cppbor::Uint{place->second},
            };
            db->put(kCurrentFileKey, current_track.toString());
          } else {
            db->put(kCurrentFileKey, "");
          }
        }
      }
    }
    if (done) {
      done();
    }
  });
}

void AudioState::react(const QueueUpdate& ev) {
  // Repeat, shuffle, clearing and adding tracks all change what should be
  // restored after a reboot. Track changes are caught by maybeSavePlace().
  if (ev.reason == QueueUpdate::kExplicitUpdate) {
    requestSave(kQueueChangeSaveDelayMs, true);
  }

  SetTrack cmd{
      .new_track = std::monostate{},
      .seek_to_second = ev.seek_to_second,
  };

  auto current = sServices->track_queue().current();
  cmd.new_track = current;

  switch (ev.reason) {
    case QueueUpdate::kExplicitUpdate:
      if (!ev.current_changed) {
        return;
      }
      break;
    case QueueUpdate::kRepeatingLastTrack:
      break;
    case QueueUpdate::kTrackFinished:
      if (!ev.current_changed) {
        cmd.new_track = std::monostate{};
      }
      break;
    case QueueUpdate::kBulkLoadingUpdate:
      // Bulk loading updates are informational only; a separate QueueUpdate
      // event will be sent when loading is done.
    case QueueUpdate::kDeserialised:
      // The current track is deserialised separately in order to retain seek
      // position.
    default:
      return;
  }

  tinyfsm::FsmList<AudioState>::dispatch(cmd);
}

void AudioState::react(const SetTrack& ev) {
  if (std::holds_alternative<std::monostate>(ev.new_track)) {
    sDecoder->open({});
    return;
  }

  // Move the rest of the work to a background worker, since it may require db
  // lookups to resolve a track id into a path.
  auto new_track = ev.new_track;
  uint32_t seek_to = ev.seek_to_second.value_or(0);
  sServices->bg_worker().Dispatch<void>([=]() {
    std::shared_ptr<TaggedStream> stream;
    if (std::holds_alternative<database::TrackId>(new_track)) {
      stream = sStreamFactory->create(std::get<database::TrackId>(new_track),
                                      seek_to);
    } else if (std::holds_alternative<std::string>(new_track)) {
      stream =
          sStreamFactory->create(std::get<std::string>(new_track), seek_to);
    }

    // Always give the stream to the decoder, even if it turns out to be empty.
    // This has the effect of stopping the current playback, which is generally
    // what the user expects to happen when they say "Play this track!", even
    // if the new track has an issue.
    sDecoder->open(stream);

    if (stream) {
      schedulePrefetch();
    }

    // ...but if the stream that failed is the front of the queue, then we
    // should advance to the next track in order to keep the tunes flowing.
    if (!stream) {
      auto& queue = sServices->track_queue();
      if (new_track == queue.current()) {
        queue.finish();
      }
    }
  });
}

void AudioState::react(const PlaySineWave& ev) {
  auto tags = std::make_shared<database::TrackTags>();

  std::stringstream title;
  title << ev.frequency << "Hz Sine Wave";
  tags->title(title.str());

  sDecoder->open(std::make_shared<TaggedStream>(
      tags, std::make_unique<SineSource>(ev.frequency), title.str()));
}

void AudioState::react(const TogglePlayPause& ev) {
  sIsPaused = !ev.set_to.value_or(sIsPaused);
  if (!sIsPaused && is_in_state<states::Standby>() &&
      sStreamCues.current().first) {
    transit<states::Playback>();
  } else if (sIsPaused && is_in_state<states::Playback>()) {
    transit<states::Standby>();
  }
}

void AudioState::react(const TtsPlaybackChanged& ev) {
  sIsTtsPlaying = ev.is_playing;
  updateOutputMode();
}

void AudioState::react(const internal::DecodingFinished& ev) {
  ESP_LOGD(kTag, "end of file decoded; awaiting playback of buffered audio");
  // If we just finished playing whatever's at the front of the queue, then we
  // need to advanve and start playing the next one ASAP in order to continue
  // gaplessly.
  sServices->bg_worker().Dispatch<void>([=]() {
    auto& queue = sServices->track_queue();
    auto current = queue.current();
    if (std::holds_alternative<std::monostate>(current)) {
      return;
    }
    auto db = sServices->database().lock();
    if (!db) {
      return;
    }
    std::string path;
    if (std::holds_alternative<std::string>(current)) {
      path = std::get<std::string>(current);
    } else if (std::holds_alternative<database::TrackId>(current)) {
      auto tid = std::get<database::TrackId>(current);
      path = db->getTrackPath(tid).value_or("");
    }
    if (path == ev.track->uri) {
      queue.finish();
      incrementPlayCount(ev.track->uri);
    }
  });
}

void AudioState::react(const internal::StreamStarted& ev) {
  if (sDrainFormat != ev.sink_format) {
    sDrainFormat = ev.sink_format;
    ESP_LOGI(kTag, "sink_format=%u ch @ %lu hz", sDrainFormat->num_channels,
             sDrainFormat->sample_rate);
  }

  sStreamCues.addCue(ev.track, ev.cue_at_sample);
  sStreamCues.update(sDrainBuffers->first.totalReceived());

  if (!sIsPaused && !is_in_state<states::Playback>()) {
    transit<states::Playback>();
  } else {
    // Make sure everyone knows we've got a track ready to go, even if we're
    // not playing it yet. This mostly matters when restoring the queue from
    // disk after booting.
    emitPlaybackUpdate(true);
  }
}

void AudioState::react(const internal::StreamEnded& ev) {
  sStreamCues.addCue({}, ev.cue_at_sample);
}

void AudioState::react(const system_fsm::HasPhonesChanged& ev) {
  if (ev.has_headphones) {
    events::Audio().Dispatch(audio::OutputModeChanged{
        .set_to = drivers::NvsStorage::Output::kHeadphones});
  } else {
    if (sServices->bluetooth().enabled()) {
      events::Audio().Dispatch(audio::OutputModeChanged{
          .set_to = drivers::NvsStorage::Output::kBluetooth});
    } else {
      // Nothing connected
      transit<states::Standby>();
    }
  }
}

void AudioState::react(const system_fsm::BluetoothEvent& ev) {
  using drivers::bluetooth::SimpleEvent;
  if (std::holds_alternative<SimpleEvent>(ev.event)) {
    auto simpleEvent = std::get<SimpleEvent>(ev.event);
    switch (simpleEvent) {
      case SimpleEvent::kConnectionStateChanged: {
        auto bt = sServices->bluetooth();
        if (bt.connectionState() !=
            drivers::Bluetooth::ConnectionState::kConnected) {
          // If BT Disconnected, move to standby state
          if (sOutput == sBtOutput) {
            events::Audio().Dispatch(audio::OutputModeChanged{
                .set_to = drivers::NvsStorage::Output::kHeadphones});
            transit<states::Standby>();
          }
          return;
        }
        auto dev = sServices->bluetooth().pairedDevice();
        if (!dev) {
          return;
        }
        events::Audio().Dispatch(audio::OutputModeChanged{
            .set_to = drivers::NvsStorage::Output::kBluetooth});
        sBtOutput->SetVolume(sServices->nvs().BluetoothVolume(dev->mac));
        events::Ui().Dispatch(VolumeChanged{
            .percent = sOutput->GetVolumePct(),
            .db = sOutput->GetVolumeDb(),
        });
        break;
      }
      default:
        break;
    }
  }
  if (std::holds_alternative<drivers::bluetooth::RemoteVolumeChanged>(
          ev.event)) {
    auto volume_chg =
        std::get<drivers::bluetooth::RemoteVolumeChanged>(ev.event).new_vol;
    events::Ui().Dispatch(RemoteVolumeChanged{.value = volume_chg});
  }
}

void AudioState::react(const StepUpVolume& ev) {
  bool changed = false;
  for (uint_fast8_t i = 0; i < std::max<uint_fast8_t>(ev.steps, 1); i++) {
    if (!sOutput->AdjustVolumeUp()) {
      break;
    }
    changed = true;
  }
  if (changed) {
    commitVolume();
    events::Ui().Dispatch(VolumeChanged{
        .percent = sOutput->GetVolumePct(),
        .db = sOutput->GetVolumeDb(),
    });
  }
}

void AudioState::react(const StepDownVolume& ev) {
  bool changed = false;
  for (uint_fast8_t i = 0; i < std::max<uint_fast8_t>(ev.steps, 1); i++) {
    if (!sOutput->AdjustVolumeDown()) {
      break;
    }
    changed = true;
  }
  if (changed) {
    commitVolume();
    events::Ui().Dispatch(VolumeChanged{
        .percent = sOutput->GetVolumePct(),
        .db = sOutput->GetVolumeDb(),
    });
  }
}

void AudioState::react(const SetVolume& ev) {
  if (ev.db.has_value()) {
    if (sOutput->SetVolumeDb(ev.db.value())) {
      commitVolume();
      events::Ui().Dispatch(VolumeChanged{
          .percent = sOutput->GetVolumePct(),
          .db = sOutput->GetVolumeDb(),
      });
    }

  } else if (ev.percent.has_value()) {
    if (sOutput->SetVolumePct(ev.percent.value())) {
      commitVolume();
      events::Ui().Dispatch(VolumeChanged{
          .percent = sOutput->GetVolumePct(),
          .db = sOutput->GetVolumeDb(),
      });
    }
  }
}

void AudioState::react(const SetVolumeLimit& ev) {
  uint16_t limit_in_dac_units =
      drivers::wm8523::kLineLevelReferenceVolume + (ev.limit_db * 4);

  sI2SOutput->SetMaxVolume(limit_in_dac_units);
  sServices->nvs().AmpMaxVolume(limit_in_dac_units);
  scheduleSettingsWrite();

  events::Ui().Dispatch(VolumeLimitChanged{
      .new_limit_db = ev.limit_db,
  });
  events::Ui().Dispatch(VolumeChanged{
      .percent = sOutput->GetVolumePct(),
      .db = sOutput->GetVolumeDb(),
  });
}

void AudioState::react(const SetVolumeBalance& ev) {
  sOutput->SetVolumeImbalance(ev.left_bias);
  sServices->nvs().AmpLeftBias(ev.left_bias);
  scheduleSettingsWrite();

  events::Ui().Dispatch(VolumeBalanceChanged{
      .left_bias = ev.left_bias,
  });
}

void AudioState::react(const OutputModeChanged& ev) {
  ESP_LOGI(kTag, "output mode changed");
  auto new_mode = sServices->nvs().OutputMode();
  if (ev.set_to) {
    new_mode = *ev.set_to;
  }
  std::shared_ptr<IAudioOutput> new_output;
  switch (new_mode) {
    case drivers::NvsStorage::Output::kBluetooth:
      new_output = sBtOutput;
      break;
    case drivers::NvsStorage::Output::kHeadphones:
      new_output = sI2SOutput;
      break;
  }
  if (new_output == sOutput) {
    return;
  }
  sOutput->mode(IAudioOutput::Modes::kOff);
  sOutput = new_output;
  sSampleProcessor->SetOutput(sOutput);
  updateOutputMode();

  // Bluetooth volume isn't 'changed' until we've connected to a device.
  if (new_mode == drivers::NvsStorage::Output::kHeadphones) {
    events::Ui().Dispatch(VolumeChanged{
        .percent = sOutput->GetVolumePct(),
        .db = sOutput->GetVolumeDb(),
    });
  }
}

auto AudioState::updateTrackData(std::string uri,
                                 std::function<void(database::TrackData&)> fn)
    -> void {
  sServices->bg_worker().Dispatch<void>([=]() {
    auto db = sServices->database().lock();
    if (!db) {
      return;
    }
    auto id = db->getTrackID(uri);
    if (!id) {
      return;
    }
    auto track = db->getTrack(*id);
    if (!track) {
      return;
    }
    auto data = track->data().clone();
    std::invoke(fn, *data);
    db->setTrackData(*id, *data);
  });
}

auto AudioState::updateSavedPosition(std::string uri,
                                     uint32_t position) -> void {
  updateTrackData(
      uri, [=](database::TrackData& data) { data.last_position = position; });
}

auto AudioState::incrementPlayCount(std::string uri) -> void {
  updateTrackData(uri, [&](database::TrackData& data) { data.play_count++; });
}

auto AudioState::updateOutputMode() -> void {
  if (is_in_state<states::Playback>() || sIsTtsPlaying) {
    sOutput->mode(IAudioOutput::Modes::kOnPlaying);
  } else {
    sOutput->mode(IAudioOutput::Modes::kOnPaused);
  }
}

auto AudioState::commitVolume() -> void {
  auto mode = sServices->nvs().OutputMode();
  auto vol = sOutput->GetVolume();
  if (mode == drivers::NvsStorage::Output::kHeadphones) {
    sServices->nvs().AmpCurrentVolume(vol);
  } else if (mode == drivers::NvsStorage::Output::kBluetooth) {
    auto dev = sServices->bluetooth().pairedDevice();
    if (!dev) {
      return;
    }
    sServices->nvs().BluetoothVolume(dev->mac, vol);
  }
  scheduleSettingsWrite();
}

// How long after a track starts to wait before reading the next track's tags.
// Waiting keeps the SD card free while the new track's buffer fills, and
// means rapid skipping doesn't trigger a read for every track passed over.
static constexpr TickType_t kPrefetchDelay = pdMS_TO_TICKS(1500);
static TimerHandle_t sPrefetchTimer = nullptr;

static void prefetch_cb(TimerHandle_t) {
  events::Audio().Dispatch(internal::PrefetchNextTrack{});
}

static auto schedulePrefetch() -> void {
  if (!sPrefetchTimer) {
    sPrefetchTimer =
        xTimerCreate("prefetch", kPrefetchDelay, false, NULL, prefetch_cb);
    if (!sPrefetchTimer) {
      return;
    }
  }
  xTimerReset(sPrefetchTimer, 0);
}

void AudioState::react(const internal::PrefetchNextTrack&) {
  sServices->bg_worker().Dispatch<void>([]() {
    auto next = sServices->track_queue().peekNext();
    if (!next) {
      return;
    }
    // The tag parser caches its results, so the stream factory will find
    // them there when this track is played.
    sServices->tag_parser().ReadAndParseTags(*next);
  });
}

static void settings_write_cb(TimerHandle_t) {
  // Timer callbacks run on the small FreeRTOS timer task. Flash writes must
  // also not happen on a task whose stack is in PSRAM (such as bg_worker),
  // since the cache is disabled during the write. Hand the write to the audio
  // task, which shares the main task's internal-RAM stack with the system FSM
  // (where Idle already calls Write()).
  events::Audio().Dispatch(internal::WriteSettings{});
}

void AudioState::react(const internal::WriteSettings&) {
  sServices->nvs().Write();
}

auto AudioState::scheduleSettingsWrite() -> void {
  if (!sSettingsWriteTimer) {
    sSettingsWriteTimer = xTimerCreate("nvs_write", kSettingsWriteDelay, false,
                                       NULL, settings_write_cb);
    if (!sSettingsWriteTimer) {
      return;
    }
  }
  // Restarts the countdown if it is already running.
  xTimerReset(sSettingsWriteTimer, 0);
}

namespace states {

void Uninitialised::react(const system_fsm::BootComplete& ev) {
  sServices = ev.services;

  sSaveTimer = xTimerCreate("save_place", pdMS_TO_TICKS(1000), false, NULL,
                            save_timer_cb);

  sDrainBuffers = std::make_unique<drivers::OutputBuffers>(
      kTrackDrainLatencySamples, kSystemDrainLatencySamples);
  sDrainBuffers->first.suspend(true);

  sStreamFactory.reset(
      new FatfsStreamFactory(sServices->database(), sServices->tag_parser(), sServices->readahead_runner()));
  sI2SOutput.reset(new I2SAudioOutput(sServices->gpios(), *sDrainBuffers));
  sBtOutput.reset(new BluetoothAudioOutput(
      sServices->bluetooth(), *sDrainBuffers, sServices->bg_worker()));

  auto& tts_provider = sServices->tts();
  auto tts_player = std::make_unique<tts::Player>(
      sServices->bg_worker(), sDrainBuffers->second, *sStreamFactory);
  tts_provider.player(std::move(tts_player));

  auto& nvs = sServices->nvs();
  sI2SOutput->SetMaxVolume(nvs.AmpMaxVolume());
  sI2SOutput->SetVolume(nvs.AmpCurrentVolume());
  sI2SOutput->SetVolumeImbalance(nvs.AmpLeftBias());

  // Always set to headphones output initially
  // Connecting bluetooth will change this
  sOutput = sI2SOutput;
  if (sServices->nvs().OutputMode() ==
      drivers::NvsStorage::Output::kBluetooth) {
    // Ensure Bluetooth gets enabled if it's the default sink.
    sServices->bluetooth().enable(true);
  }
  sOutput->mode(IAudioOutput::Modes::kOnPaused);

  events::Ui().Dispatch(VolumeLimitChanged{
      .new_limit_db =
          (static_cast<int>(nvs.AmpMaxVolume()) -
           static_cast<int>(drivers::wm8523::kLineLevelReferenceVolume)) /
          4,
  });
  events::Ui().Dispatch(VolumeChanged{
      .percent = sOutput->GetVolumePct(),
      .db = sOutput->GetVolumeDb(),
  });
  events::Ui().Dispatch(VolumeBalanceChanged{
      .left_bias = nvs.AmpLeftBias(),
  });

  sSampleProcessor.reset(new SampleProcessor(sDrainBuffers->first));
  sSampleProcessor->SetOutput(sOutput);

  sDecoder.reset(Decoder::Start(sSampleProcessor));

  // If the sd card is already mounted, load the queue here
  auto sdState = sServices->sd();
  if (sdState == drivers::SdState::kMounted) {
    loadQueue();
  }

  transit<Standby>();
}

auto Standby::entry() -> void {
  updateOutputMode();
}

void Standby::react(const system_fsm::UnmountRequest& ev) {
  bool idle = ev.idle;
  savePlaybackState([=]() {
    // The card is going away; don't let a late save touch it.
    sCanSave = false;
    events::System().Dispatch(UnmountReady{.idle = idle});
  });
}

void Standby::react(const system_fsm::SdStateChanged& ev) {
  auto state = sServices->sd();
  if (state != drivers::SdState::kMounted) {
    return;
  }
  loadQueue();
}

static TimerHandle_t sHeartbeatTimer;

static void heartbeat(TimerHandle_t) {
  events::Audio().Dispatch(internal::StreamHeartbeat{});
}

void Playback::entry() {
  ESP_LOGI(kTag, "audio output resumed");
  sDrainBuffers->first.suspend(false);
  updateOutputMode();
  emitPlaybackUpdate(false);

  if (!sHeartbeatTimer) {
    sHeartbeatTimer =
        xTimerCreate("stream", pdMS_TO_TICKS(1000), true, NULL, heartbeat);
  }
  xTimerStart(sHeartbeatTimer, portMAX_DELAY);
}

void Playback::exit() {
  ESP_LOGI(kTag, "audio output paused");
  xTimerStop(sHeartbeatTimer, portMAX_DELAY);
  sDrainBuffers->first.suspend(true);
  emitPlaybackUpdate(true);
  // Remember exactly where playback was paused.
  requestSave(kPauseSaveDelayMs, true);
}

void Playback::react(const system_fsm::SdStateChanged& ev) {
  if (sServices->sd() != drivers::SdState::kMounted) {
    transit<Standby>();
  }
}

// TEMPORARY (stutter debugging): once a second, summarise what the audio
// pipeline did. Logged at WARN so it shows at the default log level. Quiet
// seconds are only logged every 10 s.
static auto logAudioDebug(uint32_t padded_total) -> void {
  static int64_t sLast = 0;
  static uint32_t sLastPadded = 0;
  static int sQuiet = 0;
  int64_t now = esp_timer_get_time();
  if (sLast == 0) {
    sLast = now;
    sLastPadded = padded_total;
    return;
  }
  uint32_t window_us = now - sLast;
  sLast = now;

  uint32_t padded = padded_total - sLastPadded;
  sLastPadded = padded_total;

  uint32_t reads = dbg::sReadCount.exchange(0);
  uint32_t read_bytes = dbg::sReadBytes.exchange(0);
  uint32_t read_us = dbg::sReadUsTotal.exchange(0);
  uint32_t read_max = dbg::sReadUsMax.exchange(0);
  uint32_t misses = dbg::sCacheMisses.exchange(0);
  uint32_t miss_us = dbg::sCacheMissUs.exchange(0);
  uint32_t buffered_min = dbg::sBufferedMin.exchange(UINT32_MAX);
  uint32_t decode_us = dbg::sDecodeUs.exchange(0);
  uint32_t decode_max = dbg::sDecodeUsMax.exchange(0);
  uint32_t decoded = dbg::sDecodedSamples.exchange(0);

  uint32_t bus_held = drivers::dbg::sBusHeldUs.exchange(0);
  uint32_t bus_held_max = drivers::dbg::sBusHeldMax.exchange(0);
  uint32_t bus_wait = drivers::dbg::sBusWaitUs.exchange(0);
  uint32_t bus_wait_max = drivers::dbg::sBusWaitMax.exchange(0);
  uint32_t flushes = drivers::dbg::sFlushes.exchange(0);

  uint32_t decode_pct = window_us ? (uint64_t)decode_us * 100 / window_us : 0;
  bool abnormal = padded > 0 || misses > 0 || read_max > 100000 ||
                  bus_held_max > 100000 || bus_wait_max > 100000 ||
                  decode_pct > 60 || decode_max > 100000;
  if (!abnormal && ++sQuiet < 10) {
    return;
  }
  sQuiet = 0;

  ESP_LOGW("adbg",
           "%s underrun=%lu smp | decode %lu%% busy max=%lums out=%lu smp | "
           "sd reads=%lu %luKiB avg=%lums max=%lums | miss=%lu (%lums) "
           "buffered_min=%ldKiB | disp flushes=%lu held=%lums max=%lums "
           "wait=%lums max=%lums | heap int=%u/%u psram=%u",
           abnormal ? "!!" : "ok", padded, decode_pct, decode_max / 1000,
           decoded, reads, read_bytes / 1024,
           reads ? read_us / reads / 1000 : 0, read_max / 1000, misses,
           miss_us / 1000,
           buffered_min == UINT32_MAX ? -1L : (long)(buffered_min / 1024),
           flushes, bus_held / 1000, bus_held_max / 1000, bus_wait / 1000,
           bus_wait_max / 1000,
           heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
           heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

void Playback::react(const internal::StreamHeartbeat& ev) {
  logAudioDebug(sDrainBuffers->first.padded_);
  sStreamCues.update(sDrainBuffers->first.totalReceived());

  if (sStreamCues.hasStream()) {
    emitPlaybackUpdate(false);
  } else {
    // Finished the current stream, and there's nothing upcoming. We must be
    // finished.
    transit<Standby>();
  }
}

}  // namespace states

}  // namespace audio

FSM_INITIAL_STATE(audio::AudioState, audio::states::Uninitialised)
