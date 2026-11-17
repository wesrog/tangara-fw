# Tangara

> [!NOTE]
> **This is an unofficial, personal fork.** It's a hobby project where I tinker
> with the Tangara firmware for my own use. It is not affiliated with, endorsed
> by, or supported by Cool Tech Zone. For the official firmware, releases, and
> issue tracker, head to
> [cool-tech-zone/tangara-fw on Codeberg](https://codeberg.org/cool-tech-zone/tangara-fw).
> Please don't report problems with this fork upstream.
>
> That said, ideas are very welcome! If you have a feature you'd like to see,
> something that bugs you, or a tweak you've been meaning to try, please
> [open an issue](https://github.com/wesrog/tangara-fw/issues) here and
> suggest it. Pull requests are welcome too.
>
> Changes in this fork so far:
>
> - An iPod-style feel for the touch wheel: smoother scrolling, acceleration,
>   tap and hold gestures, and quicker ways to play what you're browsing.
> - Volume is saved shortly after it changes, and holding the volume buttons
>   speeds up.
> - An animated spinner in the status bar while the library is updating.
> - Six new themes: Amber CRT, Synthwave, Deep Sea, Sunset, Pea Green, and Paper.
>
> Use it at your own risk. Everything below is the original upstream README.

Tangara is a portable music player. It outputs high-quality sound through a 3.5-mm headphone jack, lasts a full day on a charge, and includes a processor that’s powerful enough to support any audio format you can throw at it. It’s also 100% open hardware running open-source software, which makes it easy to customize, repair, and upgrade. Tangara plays what you want to hear, however you want to hear it.

We're currently trying to scrape together a crowdfunding campaign! Please check us out on [Crowd Supply](https://www.crowdsupply.com/cool-tech-zone/tangara)!

## Source Repositories

[tangara-fw](https://git.sr.ht/~jacqueline/tangara-fw): The ESP32 firmware,
comprising the bulk of Tangara's functionality. This is probably the repo
that you're most interested in!

[tangara-hw](https://git.sr.ht/~jacqueline/tangara-hw): Sources for the entire
physical device. Includes both the case/enclosure, and the PCBs.

[tangara-samd-fw](https://git.sr.ht/~jacqueline/tangara-samd-fw): Firmware for
the SAMD21 co-processor that manages USB connectivity, charging, and power
management of the ESP32.

## Contributing

The firmware repositories for Tangara are mirrored on both [sourcehut](https://sr.ht/~jacqueline/tangara/) and [Codeberg](https://codeberg.org/cool-tech-zone/). Contributions on either platform are welcome; feel free to use whichever contribution workflow you prefer. If you are a [git-send-email](https://git-send-email.io/) nerd, then the list to use is [~jacqueline/tangara-devel@lists.sr.ht](mailto:~jacqueline/tangara-devel@lists.sr.ht).

For issue tracking, refer to the 'issues' tab of the relevant repo on Codeberg.

## Thanks, Aisler!

[Aisler](https://aisler.net/) have very generously sponsored our (final?) round
of pre-production prototype boards. We've gotten all of our prototype revisons
from Aisler, and we couldn't be happier with their quality. Check them out!
