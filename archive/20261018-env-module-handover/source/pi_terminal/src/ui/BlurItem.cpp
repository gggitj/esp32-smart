#include "BlurItem.h"
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLFramebufferObject>
#include <QSGTextureProvider>
#include <QSGTexture>
#include <QtQuick/qsgtexture_platform.h>
#include <QQuickOpenGLUtils>
#include <QQuickWindow>
#include <array>
#include <memory>

class BlurRenderer : public QObject, public QQuickFramebufferObject::Renderer, protected QOpenGLFunctions {
    QPointer<QSGTextureProvider> provider_;
    QOpenGLShaderProgram program_;
    std::array<std::unique_ptr<QOpenGLFramebufferObject>, 4> levels_;
    std::array<std::unique_ptr<QOpenGLFramebufferObject>, 4> upsample_;
    bool initialized_ = false;
    float strength_ = 1;
    float mix_ = 1;
    int levelCount_ = 4;
    float radius_ = 0;
    void draw(GLuint texture, QSize size, QVector2D step, GLuint original = 0, float blend = 1, float radius = 0) {
        glViewport(0, 0, size.width(), size.height());
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, original); glActiveTexture(GL_TEXTURE0);
        program_.bind(); program_.setUniformValue("image", 0); program_.setUniformValue("stepSize", step);
        program_.setUniformValue("originalImage",1); program_.setUniformValue("blendRatio",blend);
        program_.setUniformValue("outputSize",QVector2D(size.width(),size.height()));program_.setUniformValue("cornerRadius",radius);
        static const GLfloat points[] = {-1,-1, 1,-1, -1,1, 1,1};
        program_.enableAttributeArray(0); program_.setAttributeArray(0, GL_FLOAT, points, 2);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4); program_.disableAttributeArray(0); program_.release();
    }
public:
    void synchronize(QQuickFramebufferObject *item) override {
        auto *blur = static_cast<BlurItem *>(item);
        strength_ = blur->strength();
        mix_ = blur->mixAmount(); levelCount_ = blur->levels();
        radius_=blur->cornerRadius()*(blur->window()?blur->window()->effectiveDevicePixelRatio():1);
        auto *provider = blur->source() && blur->source()->isTextureProvider() ? blur->source()->textureProvider() : nullptr;
        if (provider_ != provider) {
            if (provider_) disconnect(provider_, nullptr, this, nullptr);
            provider_ = provider;
            if (provider_) connect(provider_, &QSGTextureProvider::textureChanged, this, [this] { update(); }, Qt::DirectConnection);
        }
    }
    QOpenGLFramebufferObject *createFramebufferObject(const QSize &size) override {
        return new QOpenGLFramebufferObject(size, QOpenGLFramebufferObject::NoAttachment);
    }
    void render() override {
        if (!initialized_) {
            initializeOpenGLFunctions();
            program_.addShaderFromSourceCode(QOpenGLShader::Vertex,
                "attribute vec2 vertex; varying vec2 uv; void main(){ uv=(vertex+1.0)*0.5; gl_Position=vec4(vertex,0.,1.); }");
            program_.addShaderFromSourceCode(QOpenGLShader::Fragment,
                "#ifdef GL_ES\nprecision mediump float;\n#endif\n"
                "uniform sampler2D image; uniform sampler2D originalImage; uniform float blendRatio; uniform vec2 stepSize; varying vec2 uv;"
                "uniform vec2 outputSize; uniform float cornerRadius;"
                "void main(){ vec4 c=texture2D(image,uv)*0.25;"
                "c+=(texture2D(image,uv+vec2(stepSize.x,0.))+texture2D(image,uv-vec2(stepSize.x,0.))"
                "+texture2D(image,uv+vec2(0.,stepSize.y))+texture2D(image,uv-vec2(0.,stepSize.y)))*0.125;"
                "c+=(texture2D(image,uv+stepSize)+texture2D(image,uv-stepSize)"
                "+texture2D(image,uv+vec2(stepSize.x,-stepSize.y))+texture2D(image,uv+vec2(-stepSize.x,stepSize.y)))*0.0625;"
                "if(blendRatio<1.0)c=mix(texture2D(originalImage,uv),c,blendRatio);"
                "if(cornerRadius>0.0){float r=min(cornerRadius,min(outputSize.x,outputSize.y)*0.5);"
                "vec2 q=abs((uv-0.5)*outputSize)-(outputSize*0.5-vec2(r));"
                "float d=length(max(q,vec2(0.0)))+min(max(q.x,q.y),0.0)-r;c*=1.0-smoothstep(-1.0,1.0,d);}gl_FragColor=c; }");
            program_.bindAttributeLocation("vertex", 0); initialized_ = program_.link();
        }
        glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST);
        glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT);
        if (!initialized_ || !provider_ || !provider_->texture()) { QQuickOpenGLUtils::resetOpenGLState(); return; }
        if (auto *dynamic = dynamic_cast<QSGDynamicTexture *>(provider_->texture())) dynamic->updateTexture();
        auto *native = provider_->texture()->nativeInterface<QNativeInterface::QSGOpenGLTexture>();
        if (!native) { QQuickOpenGLUtils::resetOpenGLState(); return; }
        GLuint texture = native->nativeTexture(); QSize size = framebufferObject()->size();
        QSize inputSize = provider_->texture()->textureSize();
        for (int i = 0; i < levelCount_; ++i) {
            size = QSize(qMax(1, size.width()/2), qMax(1,size.height()/2));
            if (!levels_[i] || levels_[i]->size() != size) levels_[i] = std::make_unique<QOpenGLFramebufferObject>(size);
            levels_[i]->bind(); draw(texture, size, QVector2D(strength_/qMax(1,inputSize.width()),strength_/qMax(1,inputSize.height())));
            texture = levels_[i]->texture(); inputSize = size;
        }
        for (int i = levelCount_-2; i >= 0; --i) {
            // Separate target: never sample the framebuffer being written.
            // Blend the coarse result with the retained finer mip level.
            const auto targetSize=levels_[i]->size();
            if(!upsample_[i] || upsample_[i]->size()!=targetSize)upsample_[i]=std::make_unique<QOpenGLFramebufferObject>(targetSize);
            upsample_[i]->bind();draw(texture,targetSize,QVector2D(strength_/size.width(),strength_/size.height()),levels_[i]->texture(),0.75f);
            texture=upsample_[i]->texture();size=targetSize;
        }
        framebufferObject()->bind(); draw(texture, framebufferObject()->size(), QVector2D(0,0),native->nativeTexture(),mix_,radius_);
        QQuickOpenGLUtils::resetOpenGLState();
    }
};
QQuickFramebufferObject::Renderer *BlurItem::createRenderer() const { return new BlurRenderer; }
