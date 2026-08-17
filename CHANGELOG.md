# Changelog

## [1.0.0](https://github.com/k0te1ch/powerpeek/compare/v1.0.0...v1.0.0) (2026-08-17)


### Features

* **battery:** generalise the model to devices and merge duplicates ([#27](https://github.com/k0te1ch/powerpeek/issues/27)) ([b3ff125](https://github.com/k0te1ch/powerpeek/commit/b3ff12502a3cd7549701e42f631116025f12ce7f))
* **notify:** let the user choose where notification cards appear ([#21](https://github.com/k0te1ch/powerpeek/issues/21)) ([d10e685](https://github.com/k0te1ch/powerpeek/commit/d10e6853b365072af32977a032dd383064496c06))
* PowerPeek, a Fluent device battery monitor for Windows ([7a3da8b](https://github.com/k0te1ch/powerpeek/commit/7a3da8bbb22f3c9205bf60a2b508b13025541a63))
* recolour the application icon away from Xbox green ([f8a322a](https://github.com/k0te1ch/powerpeek/commit/f8a322ad52060ba8940a2516a211381606f4edb2))
* **ui:** let the user pick the tray icon colour ([c149fb9](https://github.com/k0te1ch/powerpeek/commit/c149fb9698513eac5f4b5ddbf72b103d4e837bd2))
* window backdrop, installer, portable packaging and PowerPeek branding ([582b42d](https://github.com/k0te1ch/powerpeek/commit/582b42de53e56f00a0b95a186e15be7a3e289fbf))


### Bug fixes

* **audio:** believe the fmt chunk over the size it declares ([#19](https://github.com/k0te1ch/powerpeek/issues/19)) ([8a07f15](https://github.com/k0te1ch/powerpeek/commit/8a07f151c49fc3a67b02bb7fd8467c087b679a60))
* **audio:** handle non-finite samples instead of letting them reach lrintf ([#9](https://github.com/k0te1ch/powerpeek/issues/9)) ([70611ba](https://github.com/k0te1ch/powerpeek/commit/70611ba26d6f787191e4418d8c4afb8786d48aa2))
* **battery:** keep the charge a pad went away on for its notification ([#17](https://github.com/k0te1ch/powerpeek/issues/17)) ([99eb44e](https://github.com/k0te1ch/powerpeek/commit/99eb44e829cec386257fff4cc30055a2f1fdafe2))
* **core:** keep a described error on a single line ([#23](https://github.com/k0te1ch/powerpeek/issues/23)) ([7c10b83](https://github.com/k0te1ch/powerpeek/commit/7c10b83dceb10929929b55507a5b25ef2d859704))
* **core:** refuse a number no float can hold instead of casting it ([#26](https://github.com/k0te1ch/powerpeek/issues/26)) ([ec79873](https://github.com/k0te1ch/powerpeek/commit/ec79873bc2de2c444954466081ba650c4d3b6f7f))
* **core:** reject a string too long to convert instead of truncating its length ([#10](https://github.com/k0te1ch/powerpeek/issues/10)) ([5e81c3a](https://github.com/k0te1ch/powerpeek/commit/5e81c3a1d79118025da66fd35f7db3712071440d))
* **core:** trim the message terminator, not the message's punctuation ([#18](https://github.com/k0te1ch/powerpeek/issues/18)) ([8e6a7ee](https://github.com/k0te1ch/powerpeek/commit/8e6a7ee59166c46865a25d5f7eac711d864f8377))
* **history:** drop a sample with no usable level instead of inventing one ([#6](https://github.com/k0te1ch/powerpeek/issues/6)) ([a3bfa40](https://github.com/k0te1ch/powerpeek/commit/a3bfa40004f8f03a20b4fdff46a93f531b1dd579))
* **history:** keep samples in time order and compare against the newest reading ([#11](https://github.com/k0te1ch/powerpeek/issues/11)) ([0270431](https://github.com/k0te1ch/powerpeek/commit/02704319eb4f3375cc3a7caf582edd49a0d57458))
* **settings:** leave the critical threshold room below the low one ([#16](https://github.com/k0te1ch/powerpeek/issues/16)) ([0ccfbe7](https://github.com/k0te1ch/powerpeek/commit/0ccfbe7aeb171b77a8dcf5a7b5ea83c04b178cc2))
* **settings:** stop an older build from destroying a newer settings file ([#5](https://github.com/k0te1ch/powerpeek/issues/5)) ([85b37c2](https://github.com/k0te1ch/powerpeek/commit/85b37c2dea9432b5c17c045d1d09dc8b4e4054e0))
* **settings:** write a float setting as the number it was set to ([#14](https://github.com/k0te1ch/powerpeek/issues/14)) ([346637e](https://github.com/k0te1ch/powerpeek/commit/346637e9fbc8c2054e12336d5c56b62806fa1d4b))
* **strings:** make a half-translated row a compile error, as promised ([#7](https://github.com/k0te1ch/powerpeek/issues/7)) ([36b7f35](https://github.com/k0te1ch/powerpeek/commit/36b7f35a420394ff252d219d021cc1c8b9067bcf))
* **tools:** derive syntax-check version from version.txt ([#2](https://github.com/k0te1ch/powerpeek/issues/2)) ([1dbe237](https://github.com/k0te1ch/powerpeek/commit/1dbe2376484885e769ea866459e207e25515f352))
* **ui:** do not animate a value to the one it already rests at ([#12](https://github.com/k0te1ch/powerpeek/issues/12)) ([3b43d58](https://github.com/k0te1ch/powerpeek/commit/3b43d58afff47989868015754a0057c40a6f1f86))
* **ui:** draw the tray battery as a solid silhouette instead of an outline ([1c76003](https://github.com/k0te1ch/powerpeek/commit/1c76003e1747d2d9161ce6679295da1457d6e672))
* **ui:** sharpen the tray icon and take the level colour from the system accent ([4168a97](https://github.com/k0te1ch/powerpeek/commit/4168a9726afc87e94b774020fdfe19ed50742904))
* **ui:** snap a slider to the value its label shows ([#22](https://github.com/k0te1ch/powerpeek/issues/22)) ([106bc80](https://github.com/k0te1ch/powerpeek/commit/106bc80e6bd138ca0e8e5f97cce70ff0a8ca26d6))
* **ui:** solve the easing curve where newton alone cannot ([#20](https://github.com/k0te1ch/powerpeek/issues/20)) ([93f7442](https://github.com/k0te1ch/powerpeek/commit/93f74428c41e7ad9cc3568b952505d4466c58974))
* **ui:** stop an animation asked for the value it already shows ([#24](https://github.com/k0te1ch/powerpeek/issues/24)) ([b33546f](https://github.com/k0te1ch/powerpeek/commit/b33546f4913b294490e95da5dd0de8a816a0c693))


### Documentation

* bring the README up to date and credit the author ([1bb829b](https://github.com/k0te1ch/powerpeek/commit/1bb829ba886664d6545484bde69e3ff2cd644184))
* language-matched screenshots and a roadmap in the README ([#4](https://github.com/k0te1ch/powerpeek/issues/4)) ([efaca5b](https://github.com/k0te1ch/powerpeek/commit/efaca5b65900cc6179c5a55a6f60275c82fae9a1))
* refresh the screenshots with a device connected and the new name ([f55ce3d](https://github.com/k0te1ch/powerpeek/commit/f55ce3d231cd91a8a972553c0367aec72c74960f))
