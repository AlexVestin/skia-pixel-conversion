//
// cc glfw-opengl-example.c glad.c -lglfw
//

#define SK_GL 1

#include <chrono>
using namespace std::chrono;

#include <GLFW/glfw3.h>
#include <OpenGL/gl3.h>
#include <include/core/SkSurface.h>
#include <include/core/SkColorSpace.h>
#include <include/core/SkCanvas.h>
#include <include/core/SkPaint.h>
#include <include/core/SkPath.h>
#include <include/core/SkBitmap.h>
#include <include/core/SkPixmap.h>
#include <include/core/SkPicture.h>
#include <include/core/SkPictureRecorder.h>

#include <include/effects/SkRuntimeEffect.h>

#include <include/gpu/GrDirectContext.h>
#include <include/gpu/GrBackendSurface.h>
#include <include/gpu/GrTypes.h>
#include <include/gpu/gl/GrGLTypes.h>
#include <include/gpu/gl/GrGLInterface.h>

// skottie stuff
#include "modules/skottie/include/Skottie.h"
#include "modules/skottie/include/SkottieProperty.h"
#include "modules/skottie/utils/SkottieUtils.h"

#include <math.h>

bool fullscreen = false;
int width = 1280;
int height = 720;

#include <iostream>
#include <inttypes.h>
#include <cstdlib>
 
#define NR_COLORS 4
 
struct vec2 { 
    float x, y; 
    vec2(float v): x(v), y(v) { }
    vec2(float x, float y): x(x), y(y) { } 
};
struct vec4 { 
    float x, y, z, w; 
    vec4(): vec4(0.0) { }
    vec4(float v): x(v), y(v), z(v), w(v) { }
    vec4(float x, float y, float z, float w): x(x), y(y), z(z), w(w) { } 
    vec4 operator /(float div) {
        return vec4(x / div, y /div, z / div, w / div);
    }
    vec4 operator +(const vec4& other) {
        return vec4(x + other.x,  y + other.y,  z + other.z,  w + other.w);
    }
};
 
struct uvec2 { 
    uint32_t x, y; 
    uvec2(uint32_t v): x(v), y(v) { }
    uvec2(uint32_t x, uint32_t y): x(x), y(y) { } 
}; 
 
const vec4 conversion_y = vec4(0.25882352941, 0.50588235294, 0.09803921568, 0.06274509803);
const vec4 conversion_u = vec4(-0.14901960784, -0.29019607843, 0.43921568627, 0.50196078431);
const vec4 conversion_v = vec4(0.43921568627, -0.36862745098, -0.07058823529, 0.50196078431);
 
uvec2 index_to_position(int index, uvec2 size, int m) {
    return uvec2((index * m) % size.x, (index * m / size.x) * m);
}
 
vec4 sample_index(uint8_t* data, int index) {
    return vec4(data[index*4+0] / 255.f, data[index*4+1] / 255.f, data[index*4+2] / 255.f, data[index*4+3] / 255.f);
}
 
float dot4fv(vec4 a, vec4 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

uint8_t rgb2yuv_gpu2(int index, uvec2 size, uint8_t* image) {
 
    int ustart = size.x * size.y;
    int vstart = (ustart + ustart / 4);
 
    uvec2 position = uvec2(0);
    vec4 conv = vec4(0.0);    
    
    vec4 color;
    if (index < ustart) { 
        position = index_to_position(index, size, 1);
        conv = conversion_y;

    } else { 
        position = index_to_position(index - (index < vstart ? ustart : vstart), size, 2);
        conv = index < vstart ? conversion_u : conversion_u;

        color = color + sample_index(image, position.x + position.y * size.x) / 4.0;
        position.x = position.x + 1;
        color = color + sample_index(image, position.x + position.y * size.x) / 4.0;
        position.x = position.x - 1;
        position.y = position.y + 1;
        color = color + sample_index(image, position.x + position.y * size.x) / 4.0;
        position.x = position.x + 1;
        color = color + sample_index(image, position.x + position.y * size.x) / 4.0;
    }
 
    // When using GL_RED (8bit color size) gl_FragColor is still a vec4, but gba components are ignored
    return static_cast<uint8_t>(dot4fv(vec4(color.x, color.y, color.z, 1.0), conv) * 255);
}
 
uint8_t rgb2yuv_gpu(int index, uvec2 size, uint8_t* image) {
 
    int ustart = size.x * size.y;
    int vstart = (ustart + ustart / 4);
 
    uvec2 position = uvec2(0);
    vec4 conv = vec4(0.0);    
 
    if (index < ustart) { 
        position = index_to_position(index, size, 1);
        conv = conversion_y;
    } else if (index < vstart) { 
        position = index_to_position(index - ustart, size, 2);
        conv = conversion_u;
    } else { 
        position = index_to_position(index - vstart, size, 2);
        conv = conversion_v;
    }
 
    vec4 color = sample_index(image, position.x + position.y * size.x);
    // When using GL_RED (8bit color size) gl_FragColor is still a vec4, but gba components are ignored
    return static_cast<uint8_t>(dot4fv(vec4(color.x, color.y, color.z, 1.0), conv) * 255);
}
 
 
void rgb2yuv420p(uint8_t* rgb, uint8_t* yuv_buffer, uvec2 size) {
    uint32_t u_start = size.x * size.y;
 
    float s = size.x / 4.;
    uint32_t i    = 0; // y pos
    uint32_t upos = u_start;
    uint32_t vpos = (u_start + u_start / 4); //upos + upos / 4;
    uint8_t r, g, b;    
 
    for (uint32_t line = 0; line < size.y; line++) {
      if (!(line % 2) ) {
          for (uint32_t x = 0; x < size.x; x += 2) {
              r = rgb[NR_COLORS * i + 0];
              g = rgb[NR_COLORS * i + 1];
              b = rgb[NR_COLORS * i + 2];
              yuv_buffer[i++] = ((66*r + 129*g + 25*b) >> 8) + 16;
 
              yuv_buffer[upos++] = ((-38*r + -74*g + 112*b) >> 8) + 128;
              yuv_buffer[vpos++] = ((112*r + -94*g + -18*b) >> 8) + 128;
 
              r = rgb[NR_COLORS * i];
              g = rgb[NR_COLORS * i + 1];
              b = rgb[NR_COLORS * i + 2];
 
              yuv_buffer[i++] = ((66*r + 129*g + 25*b) >> 8) + 16;
          }
      } else {
          for (size_t x = 0; x < size.x; x += 1) {
              r = rgb[NR_COLORS * i];
              g = rgb[NR_COLORS * i + 1];
              b = rgb[NR_COLORS * i + 2];
              yuv_buffer[i++] = ((66*r + 129*g + 25*b) >> 8) + 16;
          }
      }
    }  
}



const char* sksl2 = R"(
        uniform float2 u_tex_size;
        uniform shader u_tex;
        
        float4 conversion_y = float4(0.25882352941, 0.50588235294, 0.09803921568, 0.06274509803);
        float4 conversion_u = float4(-0.14901960784, -0.29019607843, 0.43921568627, 0.50196078431);
        float4 conversion_v = float4(0.43921568627, -0.36862745098, -0.07058823529, 0.50196078431);
         
        float2 index_to_position(float index, float2 size, float m) {
            return float2(mod(index * m, size.x), floor((index * m) / size.x) * m) + float2(0.5);
        }
         
        float4 main(float2 coords) {
            float2 size = float2(u_tex_size.xy);
            float out_width = size.x + size.x / 2.;
            float2 step = (float2(1.) / size);

            float y = (size.y / 4.0 - coords.y);
            float global_index = coords.x + y * out_width - 0.5;
            
            float ustart = size.x * size.y;
            float vstart = (ustart + ustart / 4);

            float4 out_color;
            for (int i = 0; i < 4; i++) {
              float2 position;
              float4 conv;    
              float4 color;
              float index = global_index * 4 + float(i);

              if (index < ustart) { 
                position = index_to_position(index, size, 1.0);
                color = u_tex.eval(position + float2(0.5));
                conv = conversion_y;
              } else { 
                  position = index_to_position(index - (index < vstart ? ustart : vstart), size, 2.0);
                  color =  u_tex.eval(position) / 4.0;
                  color += u_tex.eval(position + float2(step.x, 0.0)) / 4.0;
                  color += u_tex.eval(position + float2(0.0, step.y)) / 4.0; 
                  color += u_tex.eval(position + step) / 4.0;
                  conv = (index < vstart) ? conversion_u : conversion_v;
              } 
              
            //   out_color[i] = float(coords.y - 1.0) / (size.y);// dot(float4(color.rgb, 1.0), conv);
            }

            out_color[0] = float(y) / 255.; // dot(float4(color.rgb, 1.0), conv);
        
            return out_color;
        }
)";



struct ColorSettings {
    ColorSettings(sk_sp<SkColorSpace> colorSpace) {
        if (colorSpace == nullptr || colorSpace->isSRGB()) {
            colorType = kRGBA_8888_SkColorType;
            pixFormat = 0x8058; //GR_GL_RGBA8;
        } else {
            colorType = kRGBA_F16_SkColorType;
            pixFormat = 0x881A; //GR_GL_RGBA16F;
        }
    }
    SkColorType colorType;
    GrGLenum pixFormat;
};


static void quit(GLFWwindow *window, int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
}


sk_sp<GrDirectContext> MakeGrContext() {
    auto interface = GrGLMakeNativeInterface();
    return GrDirectContext::MakeGL(interface);
}


sk_sp<SkSurface> MakeOnScreenGLSurface(sk_sp<GrDirectContext> dContext, int width, int height,
                                       sk_sp<SkColorSpace> colorSpace) {

    // The on-screen canvas is FBO 0. Wrap it in a Skia render target so Skia can render to it.
    GrGLFramebufferInfo info;
    info.fFBOID = 0;

    GrGLint sampleCnt = 1;
    // glGetIntegerv(GL_SAMPLES, &sampleCnt);

    GrGLint stencil = 8;
    // glGetIntegerv(GL_STENCIL_BITS, &stencil);

    if (!colorSpace) {
        colorSpace = SkColorSpace::MakeSRGB();
    }

    const auto colorSettings = ColorSettings(colorSpace);
    info.fFormat = colorSettings.pixFormat;
    GrBackendRenderTarget target(width, height, sampleCnt, stencil, info);
    sk_sp<SkSurface> surface(SkSurface::MakeFromBackendRenderTarget(dContext.get(), target,
        kBottomLeft_GrSurfaceOrigin, colorSettings.colorType, colorSpace, nullptr));
    return surface;
}


int main()
{
    glfwInit();

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    glfwWindowHint(GLFW_RED_BITS, 8);
    glfwWindowHint(GLFW_GREEN_BITS, 8);
    glfwWindowHint(GLFW_BLUE_BITS, 8);
    glfwWindowHint(GLFW_ALPHA_BITS, 8);
    glfwWindowHint(GLFW_STENCIL_BITS, 8);

    GLFWmonitor* monitor = nullptr;
    GLFWwindow *window = glfwCreateWindow(width, height, "GLFW OpenGL", monitor, NULL);
    glfwMakeContextCurrent(window);

    sk_sp<GrDirectContext> grContext = MakeGrContext();

    // int out_width = width + width / 2;
    // auto surfaceImageInfo = SkImageInfo::Make(
    //     out_width, 
    //     height / 4, 
    //     kRGBA_8888_SkColorType,// kR8_unorm_SkColorType, 
    //     SkAlphaType::kPremul_SkAlphaType,
    //     SkColorSpace::MakeSRGB()
    // );
    // sk_sp<SkSurface> surface = SkSurface::MakeRenderTarget(
    //     grContext.get(),  
    //     SkBudgeted::kYes,
    //     surfaceImageInfo,
    //     1,
    //     nullptr
    // );

    // if (surface == nullptr) {
    //     std::cout << "Failed to create surface" << std::endl;
    //     exit(1);
    // }

    sk_sp<SkSurface> surface = MakeOnScreenGLSurface(grContext, width, height, nullptr);
    SkCanvas* canvas = surface->getCanvas();

    // uvec2 size = uvec2(width, height);
    // uint32_t yuv_size = (width * height * 3) / 2;
 
    // uint8_t* image = (uint8_t*)malloc(width * height * 4);    
    // for (int i = 0; i < width * height; i++) {
    //     image[i*4+0] = std::rand() % 255;
    //     image[i*4+1] = std::rand() % 255;
    //     image[i*4+2] = std::rand() % 255;
    //     image[i*4+3] = 255;
    // }

    // uint8_t* yuv1 = (uint8_t*)malloc(yuv_size);
    // uint8_t* yuv2 = (uint8_t*)malloc(yuv_size);
    
    // rgb2yuv420p(image, yuv1, size);
 
    // for (int i = 0; i < yuv_size; i++) {
    //     yuv2[i] = rgb2yuv_gpu2(i, size, image);
    //     // if (std::abs(yuv2[i] - yuv1[i]) > 1) {
    //     //     std::cout << "Not close enough" << std::endl;
    //     //     exit(1);
    //     // }
    // }
    // // const SkImageInfo& info, void* pixels, size_t rowBytes
    // SkImageInfo info = SkImageInfo::Make(
    //     width, 
    //     height, 
    //     kRGBA_8888_SkColorType, 
    //     SkAlphaType::kPremul_SkAlphaType,
    //     SkColorSpace::MakeSRGB()
    // );
    // SkBitmap bitmap;
    // bitmap.installPixels(info, image, width * 4);
    // sk_sp<SkImage> img = SkImage::MakeFromBitmap(bitmap);
    // sk_sp<SkShader> imageShader = img->makeShader(SkSamplingOptions(SkFilterMode::kNearest));
    // auto [effect, err] = SkRuntimeEffect::MakeForShader(SkString(sksl2));
    // std::cout << "Error: " << err.c_str() << std::endl;
    // SkRuntimeEffect::ChildPtr children[] = { imageShader };
    // auto t0 = high_resolution_clock::now();
    // // SkPaint paint;
    // // vec2 uniforms = vec2(width, height);
    // // sk_sp<SkData> uniformData = SkData::MakeWithCopy(&uniforms, sizeof(uniforms));
    // // paint.setShader(effect->makeShader(std::move(uniformData), { children, 1 }));
    // // canvas->drawPaint(paint);
    // surface->flush();
    // uint8_t* d = (uint8_t*)malloc(yuv_size);
    // GrBackendRenderTarget backend = surface->getBackendRenderTarget(SkSurface::kFlushRead_BackendHandleAccess);
    // GrGLFramebufferInfo fbInfo;
    // int fboId;
    // if (backend.getGLFramebufferInfo(&fbInfo)) {
    //     fboId = fbInfo.fFBOID;
    // }
    // glBindFramebuffer(GL_FRAMEBUFFER, fboId);
    // glReadPixels(0, 0, width + width / 2, height / 4, GL_RGBA, GL_UNSIGNED_BYTE, d);

    glfwSetKeyCallback(window, quit);
    int count = 0;

    SkRect bounds = SkRect::MakeLTRB(-10000000, -100000000, 100000000, 100000000);
    SkPictureRecorder recorder;
    auto pictureCanvas = recorder.beginRecording(bounds);

    SkPaint imagePaint;
    imagePaint.setColor4f(SkColor4f::FromBytes_RGBA(0xff0000ff));
    pictureCanvas->drawRect({0, 0, 100, 100}, imagePaint);

    sk_sp<SkPicture> picture = recorder.finishRecordingAsPicture();

    std::cout << "Before loop" << std::endl;
    canvas->resetMatrix();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        // canvas->save();
        SkPaint p;
        p.setColor4f(SkColor4f::FromBytes_RGBA(0xffffffff));
        canvas->drawPaint(p);
        // auto t0 = high_resolution_clock::now();
        SkPaint paint;
        // paint.setBlendMode(SkBlendMode::kSrc);
        // paint.setAlphaf(0.3);
        // canvas->saveLayer(bounds, &paint);
        // canvas->drawPicture(picture);
        canvas->drawRect(SkRect::MakeLTRB(0, 0, 100, 100), imagePaint);
        // canvas->restore();
        canvas->flush();

        glfwSwapBuffers(window);
        count++;
    }

    
    glfwDestroyWindow(window);
    glfwTerminate();
}