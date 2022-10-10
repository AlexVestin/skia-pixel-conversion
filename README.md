```
git clone --depth 1 https://github.com/google/skia 
git clone --depth 1 https://github.com/glfw/glfw

cd skia
python3 tools/git-sync-deps
gn gen out/Static --args='is_debug=false is_official_build=true skia_use_system_expat=false skia_use_system_libjpeg_turbo=false skia_use_system_libpng=false skia_use_system_libwebp=false skia_use_system_zlib=false skia_use_system_harfbuzz=false skia_use_system_icu=false'
ninja -C out/Static

cd ..
make
```