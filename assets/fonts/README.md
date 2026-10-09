# Viewer font

Roboto Regular is bundled unmodified from Google's `googlefonts/roboto-2` repository,
commit `38062f4b4a0be4346d07a928408da21602545e9e`:
[source font](https://github.com/googlefonts/roboto-2/blob/38062f4b4a0be4346d07a928408da21602545e9e/src/hinted/Roboto-Regular.ttf).

Copyright 2011 Google Inc.; distributed under Apache License 2.0, reproduced in
`Roboto-LICENSE.txt`. No Windows system font is copied. Font SHA256:
`56a45233d29f11b4dfb86d248e921939d115778f87325e7ae8cc108383d6664d`.

CMake embeds the font into the graphical renderer at build time; runtime working
directory and separate font-file deployment are irrelevant. Dear ImGui owns glyph
atlases; the immutable embedded font bytes remain valid until program shutdown.
