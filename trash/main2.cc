#define SK_GL 1

#include <GLFW/glfw3.h>

#include <include/core/SkSurface.h>
#include <include/core/SkColorSpace.h>
#include <include/core/SkCanvas.h>
#include <include/core/SkPaint.h>
#include <include/core/SkPath.h>
#include <include/core/SkBitmap.h>
#include <include/core/SkPixmap.h>

#include <include/effects/SkRuntimeEffect.h>

#include <include/gpu/GrDirectContext.h>
#include <include/gpu/GrBackendSurface.h>
#include <include/gpu/GrTypes.h>
#include <include/gpu/gl/GrGLTypes.h>
#include <include/gpu/gl/GrGLInterface.h>

#include <math.h>

bool fullscreen = false;
int width = 300;
int height = 300;

#include <iostream>
#include <inttypes.h>
#include <cstdlib>
 
struct vec2 { 
    float x, y; 
    vec2(float v): x(v), y(v) { }
    vec2(float x, float y): x(x), y(y) { } 
};

 
const char* sksl = R"(  
uniform float2 u_tex_size;
float4 main(float2 c) {
    float s = (1. / 256.);
    return float4(c.x * s);
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


GLFWwindow* init_window() {
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
    return window;
}


int main() {
    glfwInit();
    GLFWwindow* window = init_window();

    sk_sp<GrDirectContext> grContext = MakeGrContext();
    auto surfaceImageInfo = SkImageInfo::Make(
        width, 
        height, 
        kRGBA_8888_SkColorType, 
        SkAlphaType::kOpaque_SkAlphaType
    );

    sk_sp<SkSurface> surface = SkSurface::MakeRenderTarget(
        grContext.get(),  
        SkBudgeted::kNo,
        surfaceImageInfo,
        1,
        nullptr
    );

    if (surface == nullptr) {
        std::cout << "Failed to create surface" << std::endl;
        exit(1);
    }

    SkCanvas* canvas = surface->getCanvas();
    canvas->resetMatrix();
    auto [effect, err] = SkRuntimeEffect::MakeForShader(SkString(sksl));

    SkPaint paint;
    vec2 uniforms = vec2(width, height);
    sk_sp<SkData> uniformData = SkData::MakeWithCopy(&uniforms, sizeof(uniforms));
    paint.setShader(effect->makeShader(std::move(uniformData), {}));
    
    canvas->drawPaint(paint);
    canvas->flush();

    SkBitmap readBackBitmap;
    readBackBitmap.allocPixels(surfaceImageInfo);
    surface->readPixels(readBackBitmap, 0, 0);
    uint8_t* d = static_cast<uint8_t*>(readBackBitmap.getPixels());

    int nc = 4;
    for (int i = 0; i < 100; i++) {
        std::cout << i * 4 << ": ";
        for (int j = 0; j < 4; j++) {
            std::cout << +d[(i * 4 + j) * nc] << " " ;

        }
        std::cout << std::endl;
    }

    glfwDestroyWindow(window);
    glfwTerminate();
}