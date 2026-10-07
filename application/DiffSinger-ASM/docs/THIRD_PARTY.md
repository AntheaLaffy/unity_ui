# References and external dependencies

TuneLab's JSON TLP format was used as an interchange reference at
[LiuYunPlayer/TuneLab](https://github.com/LiuYunPlayer/TuneLab), revision
`813d09ae6acbec5b25223c1e0c0c1a7ea7176a3d`, particularly
`TuneLab/Extensions/Formats/TLP/TuneLabProject.cs`. DiffSinger-ASM has its own DiffSinger-specific product direction; TLP compatibility does not define its feature roadmap. The local application is an independent implementation; no C# runtime or upstream visual asset is shipped. The reference was MIT licensed, copyright 2024 Jingang; its notice is retained in [TUNELAB_LICENSE.txt](TUNELAB_LICENSE.txt).

ASMUI is the surrounding local framework. DiffSinger-ASM remains the independent checkout exposed by `application/diffsinger`; its source/license policies remain with that project. SDL3, Vulkan, FreeType, HarfBuzz, libpng, json-c, libyaml, ICU, [MeCab and its IPA dictionary](https://taku910.github.io/mecab/), [libsndfile](https://libsndfile.github.io/libsndfile/) and [libsamplerate](https://libsndfile.github.io/libsamplerate/) are system dependencies with their own licenses. MeCab is called through its native C API; no Python reading runtime or bundled dictionary is shipped.

USTX fields and pitch units were inspected in [OpenUtau](https://github.com/stakira/OpenUtau), revision `a06ab28b0ae011bd0b85926f3044572be46a7a63`, and its [USTX format wiki](https://github.com/openutau/openutau/wiki/USTX-file-format). The C/libyaml importer is local code using the existing ASM document core. The MIT notice is retained in [OPENUTAU_LICENSE.txt](OPENUTAU_LICENSE.txt). User songs are read only for local acceptance; their lyrics, audio and full source projects are not distributed as examples.

The local Yousa voicebank and LXGW WenKai font are user-installed resources. This application does not redistribute their models, dictionaries or font files. The example project contains an authored short melody and editable text, not bundled audio or a proprietary voice model.

OpenUtau's pinned `DiffSingerJapanesePhonemizer`, `DiffSingerBasePhonemizer`, `UNote` and `UPart` were additionally inspected for kana reading, bracketed phonetic hints, ordinary `+` syllable progression and `+~` / `+*` vowel extension. The local frontend uses the bank's actual metadata and native ICU/MeCab APIs. This reference does not restore the OpenUtau consumer runtime or removed engine ABI/protocol.
