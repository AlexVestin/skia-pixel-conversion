```
git clone https://github.com/google/skia 
git clone https://github.com/glfw/glfw

gn gen out/Static --args='is_debug=false is_official_build=true skia_use_system_expat=false skia_use_system_libjpeg_turbo=false skia_use_system_libpng=false skia_use_system_libwebp=false skia_use_system_zlib=false

ninja -C out/Static
```