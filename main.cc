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
#include "modules/skparagraph/include/Paragraph.h"
#include "modules/skparagraph/include/ParagraphBuilder.h"
#include "modules/skparagraph/src/ParagraphBuilderImpl.h"
#include "modules/skparagraph/src/ParagraphPainterImpl.h"
#include "modules/skunicode/include/SkUnicode.h"
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
#include "modules/skresources/include/SkResources.h"
#include "include/utils/SkTextUtils.h"
#include <filesystem>

#include <math.h>


bool fullscreen = false;
int width = 1280;
int height = 720;

#include <map>
#include <iostream>
#include <inttypes.h>
#include <cstdlib>
#include <sstream>
 
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

class Logger  : public skottie::Logger {
public:
    struct LogEntry {
        SkString fMessage, fJSON;
    };

    void log(skottie::Logger::Level lvl, const char message[], const char json[]) override {
        auto& log = lvl == skottie::Logger::Level::kError ? fErrors : fWarnings;
        log.push_back({ SkString(message), json ? SkString(json) : SkString() });
    }

    void report() const {
        SkDebugf("Animation loaded with %zu error%s, %zu warning%s.\n",
                    fErrors.size(), fErrors.size() == 1 ? "" : "s",
                    fWarnings.size(), fWarnings.size() == 1 ? "" : "s");

        const auto& show = [](const LogEntry& log, const char prefix[]) {
            SkDebugf("%s%s", prefix, log.fMessage.c_str());
            if (!log.fJSON.isEmpty())
                SkDebugf(" : %s", log.fJSON.c_str());
            SkDebugf("\n");
        };

        for (const auto& err : fErrors)   show(err, "  !! ");
        for (const auto& wrn : fWarnings) show(wrn, "  ?? ");
    }

private:
    std::vector<LogEntry> fErrors,
                            fWarnings;
};



class TypefaceProvider final : public skresources::ResourceProviderProxyBase {
public:
    explicit TypefaceProvider(sk_sp<skresources::ResourceProvider> rp)
        : INHERITED(std::move(rp)) {}


    sk_sp<SkTypeface> loadTypeface(const char* fontName, const char* styleName) const override {
        // std::cout << "Getting typeface: " << fontName << " style: " << styleName << std::endl;
        std::string font{fontName};
        if (fonts.count(font) == 0) {
            return nullptr;
        }
        return fonts.at(font);
    }

    void registerTypeface(const char* fontName, sk_sp<SkTypeface> typeface) {
        fonts[std::string(fontName)] = typeface;
    }
private:
    std::map<std::string, sk_sp<SkTypeface>> fonts;

    using INHERITED = skresources::ResourceProviderProxyBase;
};
 

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


int fontSize = 0;
int boxWidth = 0;
int boxHeight = 0;
int lineHeight = 0;
int iteration = 0;
int vAlign = 0;
int hAlign = 0;
int resize = 0;
int color = 0;

void GLFWKeyboardEventCallback(GLFWwindow* window,  int key, int scancode, int action, int mods) {
  if (action == GLFW_PRESS) {
    switch(key) {
      case GLFW_KEY_A: fontSize--; break;
      case GLFW_KEY_S: fontSize++; break;
      case GLFW_KEY_Q: boxWidth++; break; 
      case GLFW_KEY_W: boxWidth--; break;
      case GLFW_KEY_E: boxHeight++; break; 
      case GLFW_KEY_R: boxHeight--; break;
      case GLFW_KEY_Z: lineHeight++; break;
      case GLFW_KEY_X: lineHeight--; break;
      case GLFW_KEY_RIGHT: iteration++; break;
      case GLFW_KEY_LEFT: iteration--; break;
      case GLFW_KEY_UP: vAlign++; break;
      case GLFW_KEY_DOWN: hAlign++; break;
      case GLFW_KEY_P: resize++; break;
      case GLFW_KEY_C: color++; break;
    }
  }

  if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
    glfwSetWindowShouldClose(window, GLFW_TRUE);
  }
}


std::string remove_r(const char* str, int size) {
    std::string s;
    for (int i = 0; i < size; i++) {
        s += str[i] == '\r' ? ' ' : str[i];
    }
    return s;
}


int main(int argc, char *argv[]) {
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
    glfwSetKeyCallback(window, GLFWKeyboardEventCallback);
    

    sk_sp<GrDirectContext> grContext = MakeGrContext();
    sk_sp<SkSurface> surface = MakeOnScreenGLSurface(grContext, width, height, nullptr);
    SkCanvas* canvas = surface->getCanvas();

    auto logger = sk_make_sp<Logger>();
    auto weight = SkFontStyle::kNormal_Weight;
    auto slant  = SkFontStyle::kUpright_Slant;
    auto style = SkFontStyle(weight, SkFontStyle::kNormal_Width, slant);
    
    // SkFontStyleSet styleSet = SkFontMgr::RefDefault()->createStyleSet(0);
    class NullResourceProvider final : public skottie::ResourceProvider {
        sk_sp<SkData> load(const char[], const char[]) const override { return nullptr; }
    };
    auto resource_provider = sk_make_sp<TypefaceProvider>(sk_make_sp<NullResourceProvider>());


    std::vector<sk_sp<SkTypeface>> typefaces;
    for (const auto & entry : std::filesystem::directory_iterator("fonts/")) {
        sk_sp<SkTypeface> typeface = SkFontMgr::RefDefault()->makeFromFile(entry.path().c_str());
        SkString familyName;
        typeface->getFamilyName(&familyName);
        // std::cout << entry.path().c_str() << " " << familyName.c_str() << std::endl;
        resource_provider->registerTypeface(familyName.c_str(), typeface);
        typefaces.push_back(typeface);
    }

    auto mgr = std::make_unique<skottie_utils::CustomPropertyManager>(
        skottie_utils::CustomPropertyManager::Mode::kCollapseProperties, 
        ""
    );
    

    uint32_t flags = 0;
    if (false) {
        flags |= skottie::Animation::Builder::kPreferEmbeddedFonts;
    }
    skottie::Animation::Builder builder(flags);
    builder.setLogger(logger);
    builder.setResourceProvider(resource_provider);
    builder.setPropertyObserver(mgr->getPropertyObserver());
    builder.setMarkerObserver(mgr->getMarkerObserver());

    for(auto& m: mgr->markers()) {
        // std::cout << "Marker: " << m.name << " [" << m.t0 << "," << m.t1 << "]" << std::endl; 
    }
    
    std::string fPath;
    if (argc == 1) {
        fPath = "lotties/06_Flicker/data.json";
    } else {
        fPath = std::string("lotties/") + std::string(argv[1]) + std::string("/data.json");
    }
   

    std::cout << "Loading: " << fPath << std::endl;
    auto fAnimation = builder.makeFromFile(fPath.c_str());
    auto fAnimationStats = builder.getStats();
    float animationWidth = fAnimation->size().fWidth;
    float animationHeight = fAnimation->size().fHeight;

    
    
    if (fAnimation) {
        fAnimation->seek(0);
        SkDebugf("Loaded Bodymovin animation v: %s, size: [%f %f]\n",
                 fAnimation->version().c_str(),
                 fAnimation->size().width(),
                 fAnimation->size().height());
        logger->report();
    } else {
        SkDebugf("failed to load Bodymovin animation: %s\n", fPath.c_str());
        exit(1);
    }

    skia::textlayout::TextStyle tstyle;
    SkPaint ppaint;
    ppaint.setColor4f(SkColor4f::FromBytes_RGBA(0xff0000ff));
    tstyle.setForegroundColor(ppaint);
    // tstyle.setFontFamilies({SkString("")});
    tstyle.setFontSize(30);
    skia::textlayout::ParagraphStyle paraStyle;
    paraStyle.setTextStyle(tstyle);
    

    auto collection = sk_make_sp<skia::textlayout::FontCollection>();
    collection->setDefaultFontManager(SkFontMgr::RefDefault());
    auto pb = skia::textlayout::ParagraphBuilderImpl::make(paraStyle, collection);
    pb->addText("Hello this is SkParagraph;");
    auto paragraph = pb->Build();
    paragraph->layout(2000.0);
    auto paragraphPainter = new skia::textlayout::CanvasParagraphPainter(canvas);

    
    auto lr = paragraph->getTightLineBounds(0);

    int count = 0;
    canvas->resetMatrix();

    const auto dstR = SkRect::MakeLTRB(0, 0, width, height);

    int fCurrentFrame = 0;

    SkRect outline;
    skottie::TextPropertyValue initialValues;
    std::cout << "num: " << mgr->getTextProps().size() << std::endl;
    for (const auto& cp : mgr->getTextProps()) {
        initialValues = mgr->getText(cp);
        printf("text: %s \n", remove_r(initialValues.fText.c_str(), initialValues.fText.size()).c_str());
        outline = initialValues.fBox;
        vAlign = static_cast<int>(initialValues.fVAlign);
        hAlign = static_cast<int>(initialValues.fHAlign);
        break;
    }

    grContext->flushAndSubmit(true);
    glfwSwapBuffers(window);
    canvas->resetMatrix();

    std::vector<uint32_t> colors = { 0xff000000, 0xff12abcc, 0xffff0000, 0xfff0ffdd};

    SkFont font = SkFont(typefaces[0]);
    // font.setBaselineSnap(false);
    // font.setSubpixel(true);
    font.setLinearMetrics(true);

    



    sk_sp<SkTextBlob> textBlob = SkTextBlob::MakeFromString("This is a textblob", font);

    bool hasText = true;
    std::cout << "Subpixel: " << font.isSubpixel() << std::endl;
    bool drawParagraph = true;
    
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        double time = glfwGetTime();
        
        canvas->clear(SkColor4f::FromBytes_RGBA(0xffffffff));
        // canvas->clear(SkColor4f::FromBytes_RGBA(0xff));
        SkPaint paint;
        SkPoint anchorPoint;

        if (drawParagraph) {
            canvas->save();

            canvas->translate(200, 200);
            SkPaint bgPaint;
            bgPaint.setColor4f(SkColor4f::FromBytes_RGBA(0xffff00ff));


            canvas->drawRect(lr, bgPaint);
            paragraph->paint(paragraphPainter, 0, 0);
            
            // canvas->translate(-200, -200);
            canvas->restore();

            // canvas->save();
            // canvas->scale(1.6, 1.6);
            // ppaint.setAntiAlias(true);
            // SkPaint bg;
            // bg.setAntiAlias(true);
            // bg.setColor4f(SkColor4f::FromBytes_RGBA(0x33ff0099));

            // float y = 100 ;
            // float x = 100;// + time * 2.0;
            // canvas->translate(0, time * 2.0);
            
            // canvas->drawRect(SkRect::MakeLTRB(x - 20, y - 20, x, y), bg);
            // ppaint.setDither(true);
            // canvas->drawTextBlob(textBlob, x, y, ppaint);
            // canvas->restore();
        }
        

        // if (hasText) {
        //     for (const auto& cp: mgr->getTransformProps()) {
        //         auto t = mgr->getTransform(cp);
        //         mgr->setTransform(cp, t);
        //         anchorPoint = t.fAnchorPoint;
        //         break;
        //     }

        //     for (const auto& cp: mgr->getOpacityProps()) {
        //         auto t = mgr->getOpacity(cp);
        //         mgr->setOpacity(cp, 100.f);
        //         break;
        //     }

        //     for (const auto& cp: mgr->getColorProps()) {
        //         auto t = mgr->getColor(cp);
        //         mgr->setColor(cp, colors[color % colors.size()]);
        //         break;
        //     }
        //     for (const auto& cp : mgr->getTextProps()) {
        //         auto t = mgr->getText(cp);
        //         t.fTypeface = typefaces[iteration % typefaces.size()];  
        //         t.fTextSize = initialValues.fTextSize + fontSize * 10;
        //         // t.fLineHeight = initialValues.fLineHeight + lineHeight * 8;
        //         t.fLineHeight = initialValues.fLineHeight + fontSize * 10 + lineHeight * 8;
        //         t.fHasStroke = true;
        //         t.fStrokeColor = SkColor(0xffff0000);
        //         t.fVAlign = static_cast<skottie::Shaper::VAlign>(vAlign % 5);
        //         t.fHAlign = static_cast<SkTextUtils::Align>(hAlign % 3);
        //         t.fResize = static_cast<skottie::Shaper::ResizePolicy>(resize % 3);

        //         t.fFillColor = colors[color % colors.size()];
                
        //         // t.fStrokeWidth = iteration;
        //         t.fPaintOrder = skottie::TextPaintOrder::kStrokeFill;

        //         float widthChange = boxWidth * 40.f;
        //         float heightChange = boxHeight * 40.f;
        //         float newWidth = initialValues.fBox.width() + widthChange; 
        //         float newHeight = initialValues.fBox.height() + heightChange; 
        //         t.fBox = SkRect::MakeXYWH(initialValues.fBox.x() - widthChange / 2.f, initialValues.fBox.y() - heightChange / 2.f, newWidth, newHeight);  
            
        //         outline = t.fBox;
        //         mgr->setText(cp, t);
        //     }

        //     SkPaint outlinePaint;
        //     canvas->save();
        //     float sx = height / animationWidth;
        //     float sy = height / animationHeight;
        //     canvas->translate(width / 2 - anchorPoint.fX * sx, height / 2 - anchorPoint.fY * sy);
        //     canvas->scale(sx, sy);
        //     outlinePaint.setARGB(100, 100, 100, 100);
        //     canvas->drawRect(outline, outlinePaint);
        //     canvas->restore();
        // }
        
        // fAnimation->seekFrameTime(std::fmod(time, fAnimation->duration()), nullptr);
        // auto flags = skottie::Animation::RenderFlag::kDisableTopLevelClipping | skottie::Animation::RenderFlag::kSkipTopLevelIsolation;
        // fAnimation->render(canvas, &dstR, flags);
        // fCurrentFrame++;
        
        canvas->flush();        
        grContext->flushAndSubmit(true);
        glfwSwapBuffers(window);
        count++;
    }

    
    glfwDestroyWindow(window);
    glfwTerminate();
}


//   int currentIteration = static_cast<int>(time / fAnimation->duration());
//         if (currentIteration > iteration) {
//             for (const auto& cp : mgr->getTextProps()) {
//                 auto t = mgr->getText(cp);
//                 // t.fTextSize *= 1.f + (count % 200) / 5.f;
//                 t.fTypeface = typefaces[currentIteration % typefaces.size()];  
                
//                 iteration++;
                
//                 if (iteration % typefaces.size() == 0) {
//                     t.fTextSize /= 2;
//                     t.fResize =  skottie::Shaper::ResizePolicy::kScaleToFit;
//                     t.fLineHeight /= 2.f;
//                     std::cout << t.fMaxLines << std::endl;
//                     t.fBox = SkRect::MakeXYWH(t.fBox.x() + t.fBox.width() / 4.0, t.fBox.y() - t.fBox.height() / 2.0, t.fBox.width() / 2.0, t.fBox.height() * 2.0 );  
//                 }
//                 outline = t.fBox;
//                 mgr->setText(cp, t);
//             }
//         }