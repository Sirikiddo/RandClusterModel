#include "renderers/TerrainRenderer.h"
#include "../DebugMacros.h"

TerrainRenderer::TerrainRenderer(QOpenGLFunctions_3_3_Core* gl,
                                 GLuint program,
                                 GLint uMvp,
                                 GLint uModel,
                                 GLint uLightDir,
                                 GLuint vao,
                                 GLsizei indexCount)
    : gl_(gl)
    , program_(program)
    , uMvp_(uMvp)
    , uModel_(uModel)
    , uLightDir_(uLightDir)
    , vao_(vao)
    , indexCount_(indexCount) {}

void TerrainRenderer::render(const HexSphereRenderer::RenderContext& ctx) const {
    if (indexCount_ == 0 || program_ == 0 || vao_ == 0) {
        DEBUG_CALL_PARAM("SKIP - invalid state: indexCount=" << indexCount_
            << " program=" << program_ << " vao=" << vao_);
        return;
    }

    DEBUG_CALL_PARAM("indexCount=" << indexCount_ << " program=" << program_);

    // ============== œ–Œ¬≈– ¿ œ–Œ√–¿ÃÃ€ ==============
    if (!gl_->glIsProgram(program_)) {
        DEBUG_CALL_PARAM("ERROR: program " << program_ << " is not a valid program!");
        return;
    }

    GLint linkStatus;
    gl_->glGetProgramiv(program_, GL_LINK_STATUS, &linkStatus);
    if (linkStatus != GL_TRUE) {
        DEBUG_CALL_PARAM("ERROR: Program not linked!");
        char infoLog[1024];
        gl_->glGetProgramInfoLog(program_, sizeof(infoLog), nullptr, infoLog);
        DEBUG_CALL_PARAM("Program info log: " << infoLog);
        return;
    }

    // ============== »—œŒÀ‹«Œ¬¿Õ»≈ œ–Œ√–¿ÃÃ€ ==============
    GLint currentProgram;
    gl_->glGetIntegerv(GL_CURRENT_PROGRAM, &currentProgram);
    DEBUG_CALL_PARAM("Current program before: " << currentProgram);

    gl_->glUseProgram(program_);

    GLenum err = gl_->glGetError();
    if (err != GL_NO_ERROR) {
        DEBUG_CALL_PARAM("OpenGL Error after glUseProgram: " << err);
        return;
    }

    // ============== ”—“¿ÕŒ¬ ¿ UNIFORM ==============
    DEBUG_CALL_PARAM("Uniform locations - uMvp_: " << uMvp_
        << " uModel_: " << uModel_
        << " uLightDir_: " << uLightDir_);

    if (uMvp_ != -1) {
        gl_->glUniformMatrix4fv(uMvp_, 1, GL_FALSE, ctx.mvp.constData());
    }
    else {
        DEBUG_CALL_PARAM("WARNING: uMvp_ uniform not found!");
    }

    if (uModel_ != -1) {
        QMatrix4x4 model;
        model.setToIdentity();
        gl_->glUniformMatrix4fv(uModel_, 1, GL_FALSE, model.constData());
    }

    if (uLightDir_ != -1) {
        const QVector3D& lightDir = ctx.lighting.direction;
        gl_->glUniform3f(uLightDir_, lightDir.x(), lightDir.y(), lightDir.z());
    }

    err = gl_->glGetError();
    if (err != GL_NO_ERROR) {
        DEBUG_CALL_PARAM("OpenGL Error after setting uniforms: " << err);
    }

    // ============== Õ¿—“–Œ… ¿ VAO ==============
    GLint currentVAO;
    gl_->glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &currentVAO);
    DEBUG_CALL_PARAM("Current VAO before: " << currentVAO << " expected: " << vao_);

    gl_->glBindVertexArray(vao_);

    gl_->glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &currentVAO);
    if (currentVAO != (GLint)vao_) {
        DEBUG_CALL_PARAM("ERROR: VAO binding failed! Got: " << currentVAO);
    }

    // ============== œ–Œ¬≈– ¿ ¡”‘≈–Œ¬ ==============
    GLint elementBuffer;
    gl_->glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &elementBuffer);
    DEBUG_CALL_PARAM("Element buffer bound: " << elementBuffer);

    GLint bufferSize;
    gl_->glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &bufferSize);
    DEBUG_CALL_PARAM("Element buffer size: " << bufferSize << " bytes");
    DEBUG_CALL_PARAM("Expected: " << (indexCount_ * sizeof(uint32_t)) << " bytes for " << indexCount_ << " indices");

    if (bufferSize < indexCount_ * sizeof(uint32_t)) {
        DEBUG_CALL_PARAM("ERROR: Element buffer too small!");
    }

    // ============== Œ“À¿ƒŒ◊Õ€… –≈∆»Ã ==============
    // —Óı‡ÌˇÂÏ ÚÂÍÛ˘ËÈ ÂÊËÏ ÔÓÎË„ÓÌÓ‚
    GLint polygonMode[2];
    gl_->glGetIntegerv(GL_POLYGON_MODE, polygonMode);
    DEBUG_CALL_PARAM("Original polygon mode: front=" << polygonMode[0] << " back=" << polygonMode[1]);

    // ¬ÂÏÂÌÌÓ ‚ÍÎ˛˜‡ÂÏ wireframe ÂÊËÏ ‰Îˇ ÓÚÎ‡‰ÍË
    // –‡ÒÍÓÏÏÂÌÚËÛÈÚÂ ÒÎÂ‰Û˛˘Û˛ ÒÚÓÍÛ ˜ÚÓ·˚ Û‚Ë‰ÂÚ¸ wireframe
    // gl_->glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    // ============== Œ“–»—Œ¬ ¿ ==============
    DEBUG_CALL_PARAM("calling glDrawElements with " << indexCount_ << " indices");
    gl_->glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);

    // ¬ÓÁ‚‡˘‡ÂÏ ÓË„ËÌ‡Î¸Ì˚È ÂÊËÏ ÂÒÎË ÏÂÌˇÎË
    // gl_->glPolygonMode(GL_FRONT_AND_BACK, polygonMode[0]);

    // ============== œ–Œ¬≈– ¿ Œÿ»¡Œ  œŒ—À≈ Œ“–»—Œ¬ » ==============
    err = gl_->glGetError();
    if (err != GL_NO_ERROR) {
        DEBUG_CALL_PARAM("OpenGL Error after draw: " << err);
    }
    else {
        DEBUG_CALL_PARAM("glDrawElements successful");

        // ƒÓÔÓÎÌËÚÂÎ¸Ì‡ˇ ÔÓ‚ÂÍ‡ - ÒÍÓÎ¸ÍÓ ÔËÏËÚË‚Ó‚ ·˚ÎÓ ÓÚËÒÓ‚‡ÌÓ
        GLint primitivesGenerated = 0;
        gl_->glGetIntegerv(GL_PRIMITIVES_GENERATED, &primitivesGenerated);
        DEBUG_CALL_PARAM("Primitives generated: " << primitivesGenerated);
    }

    // ============== —¡–Œ— ==============
    gl_->glBindVertexArray(0);

    // œÓ‚ÂˇÂÏ ˜ÚÓ VAO ‰ÂÈÒÚ‚ËÚÂÎ¸ÌÓ Ò·Ó¯ÂÌ
    gl_->glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &currentVAO);
    DEBUG_CALL_PARAM("Current VAO after reset: " << currentVAO);
}

