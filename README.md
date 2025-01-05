# Half-Life 2 HDR Mod
Enable scRGB output on Half-Life 2, And disable game's original post-processing and add our own hdr post processing

The hdr postprocessing code maybe not good, feel free to improve it by yourself.

Console Commands For Fine Tuning Hdr Output:
- hdr_scene_brightness_scale
- hdr_screen_nits
- hdr_hud_nits

### Usage
Extract to Half-Life 2 Folder and run hl2-hdr.exe to start the game.

### Screenshots
![screenshot1](./assets/screenshot1.avif)
![screenshot2](./assets/screenshot2.avif)
![screenshot3](./assets/screenshot3.avif)
![screenshot4](./assets/screenshot4.avif)

### About modified DXVK:
I just forced swapchain and backbuffer format to ARGB16F and specify scRGB color space, the DXVK-HDR fork always freezes my games somehow so I modified it from master branch.

### License
Some code was from Source SDK 2013, so they are [SOURCE 1 SDK LICENSE](./LICENSE_SOURCE)

[MinHook License](https://github.com/TsudaKageyu/minhook/blob/master/LICENSE.txt)

Others are just public domain, I don't care
