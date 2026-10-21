#include "tests/WaterScreenPassTests.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QElapsedTimer>
#include <QTextStream>
#include <Windows.h>
#include <Psapi.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

#include "renderers/WaterRenderer.h"
#include "generation/MeshGenerators/WaterMeshGenerator.h"
#include "ui/PlanetSettingsPanel.h"

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

QByteArray shaderSource(const QString& path, int depth = 0) {
    require(depth < 16, "Shader include recursion");
    QFile file(path);
    require(file.open(QIODevice::ReadOnly | QIODevice::Text), "Cannot read water shader");
    QByteArray result;
    for (const auto& line : file.readAll().split('\n')) {
        const auto trimmed = line.trimmed();
        if (trimmed.startsWith("#include \"")) {
            result += shaderSource(QDir(QFileInfo(path).path()).filePath(
                QString::fromUtf8(trimmed.split('"').at(1))), depth + 1);
        } else result += line + '\n';
    }
    return result;
}

void compile(QOpenGLShaderProgram& program, const QByteArray& vertex, const QByteArray& fragment) {
    if (!program.addShaderFromSourceCode(QOpenGLShader::Vertex, vertex)
        || !program.addShaderFromSourceCode(QOpenGLShader::Fragment, fragment)
        || !program.link()) {
        qWarning().noquote() << program.log();
        throw std::runtime_error("Water shader compile/link failed");
    }
}

GLuint constantCube(QOpenGLFunctions_3_3_Core& gl, float value) {
    GLuint texture = 0;
    gl.glGenTextures(1, &texture);
    gl.glBindTexture(GL_TEXTURE_CUBE_MAP, texture);
    const float rgba[]{value, value, value, 1.0f};
    for (int face = 0; face < 6; ++face)
        gl.glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGBA32F,
            1, 1, 0, GL_RGBA, GL_FLOAT, rgba);
    gl.glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl.glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl.glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl.glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    return texture;
}

struct Pixels {
    std::vector<unsigned char> color;
    std::vector<float> depth;
};
}

bool runWaterScreenPassTests() {
    try {
        PlanetSettingsPanel settings;
        auto* wavesCheck = settings.findChild<QCheckBox*>("waterWavesEnabled");
        auto* presetBox = settings.findChild<QComboBox*>("waterPreset");
        auto* waterGroup = settings.findChild<QGroupBox*>("waterSettingsGroup");
        require(wavesCheck && presetBox && waterGroup, "Missing water settings controls");
        waterGroup->setChecked(true);
        require(settings.currentWaterParams().wavesEnabled, "Waves must be on by default");
        int updates = 0;
        bool lastWavesEnabled = true;
        QObject::connect(&settings, &PlanetSettingsPanel::waterParamsChanged,
            [&updates, &lastWavesEnabled](const WaterParams& params) {
                ++updates;
                lastWavesEnabled = params.wavesEnabled;
            });
        wavesCheck->setChecked(false);
        require(updates == 1 && !lastWavesEnabled, "Waves checkbox did not emit disabled state");
        presetBox->setCurrentIndex(1);
        require(!settings.currentWaterParams().wavesEnabled, "Preset unexpectedly re-enabled waves");
        const auto savedWater = settings.currentWaterParams();
        wavesCheck->setChecked(true);
        require(lastWavesEnabled && settings.currentWaterParams().shellWaveAmplitude == savedWater.shellWaveAmplitude,
            "Re-enabling waves lost the saved wave settings");
        settings.setContributorMode(true);
        require(!wavesCheck->isEnabled(), "Contributor mode left water toggle enabled");
        settings.setContributorMode(false);
        require(wavesCheck->isEnabled(), "Water toggle not restored after contributor mode");
        wavesCheck->setChecked(false);
        settings.resize(380, 900);
        settings.grab().save("build/Planet/water-settings-flat.png");

        QOpenGLContext context;
        context.setFormat(QSurfaceFormat::defaultFormat());
        require(context.create(), "Cannot create OpenGL context");
        QOffscreenSurface surface;
        surface.setFormat(context.format());
        surface.create();
        require(context.makeCurrent(&surface), "Cannot make OpenGL context current");
        QOpenGLFunctions_3_3_Core gl;
        require(gl.initializeOpenGLFunctions(), "OpenGL 3.3 unavailable");
        qInfo() << "Water screen pass GPU:" << reinterpret_cast<const char*>(gl.glGetString(GL_RENDERER));

        const auto vertex = shaderSource(":/Planet/resources/water/water_shell.vert");
        const auto fragment = shaderSource(":/Planet/resources/water/water_shell.frag");
        const QByteArray quadVertex = R"(#version 330 core
void main() {
    const vec2 p[6] = vec2[6](vec2(-1,-1), vec2(1,-1), vec2(-1,1),
                              vec2(-1,1), vec2(1,-1), vec2(1,1));
    gl_Position = vec4(p[gl_VertexID], 0, 1);
})";
        QOpenGLShaderProgram triangleProgram, quadProgram;
        compile(triangleProgram, vertex, fragment);
        compile(quadProgram, quadVertex, fragment);

        HexSphereSceneController scene;
        scene.setSubdivisionLevel(2);
        auto water = scene.waterParams();
        water.preset = WaterPreset::Storm;
        scene.setWaterParams(water);
        const float seaRadius = scene.model().waterSurfaceRadius();
        ecs::ComponentStorage entities;
        const HexSphereRenderer::RenderGraph graph{scene, entities, scene.heightStep()};
        WaterRenderer triangle(&gl, triangleProgram.programId()), quad(&gl, quadProgram.programId());
        GLuint vao = 0, depthTexture = 0;
        gl.glGenVertexArrays(1, &vao);
        gl.glGenTextures(1, &depthTexture);
        gl.glBindTexture(GL_TEXTURE_2D, depthTexture);
        const float backgroundDepth = 1.0f;
        gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, 1, 1, 0, GL_RED, GL_FLOAT, &backgroundDepth);
        gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        const GLuint environment = constantCube(gl, 0.4f);
        const GLuint radius = constantCube(gl, seaRadius - 0.1f);
        const GLuint kind = constantCube(gl, 3.0f);
        const GLuint shore = constantCube(gl, 1.0f);
        const WaterRenderer::Resources resources{environment, depthTexture, radius, kind, shore, vao};

        // Odd, non-square viewport catches edge coverage and aspect-ratio mistakes.
        const QSize size(321, 239);
        QOpenGLFramebufferObjectFormat format;
        format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        QOpenGLFramebufferObject resolved(size, format);
        require(resolved.isValid(), "Cannot create resolve framebuffer");
        for (const int samples : {0, 4}) {
            format.setSamples(samples);
            QOpenGLFramebufferObject target(size, format);
            require(target.isValid(), "Cannot create test framebuffer");
            require(target.format().samples() >= samples, "Requested MSAA unavailable");
            require(target.bind(), "Cannot bind test framebuffer");
            gl.glViewport(0, 0, size.width(), size.height());
            gl.glClearColor(0, 0, 0, 0);
            const size_t pixelCount = static_cast<size_t>(size.width()) * size.height();
            size_t totalHits = 0, troughHits = 0, beyondMeanHorizon = 0;

            for (int view = 0; view < 3; ++view) {
                const QVector3D eye = view == 0 ? QVector3D(0, 0, seaRadius * 3.0f)
                    : QVector3D(0, seaRadius * 1.12f, 0);
                const QVector3D aim = view == 0 ? QVector3D(0, 0, 0)
                    : QVector3D(0, seaRadius * 0.85f, -seaRadius);
                HexSphereRenderer::RenderCamera camera;
                camera.view.lookAt(eye, aim, view == 0 ? QVector3D(0, 1, 0) : QVector3D(0, 0, -1));
                camera.projection.perspective(45, float(size.width()) / size.height(), 0.001f, 20.0f);
                const auto vp = camera.projection * camera.view;
                HexSphereRenderer::SceneLighting lighting{QVector3D(1, 1, 1).normalized(), view * 17.0};
                const HexSphereRenderer::RenderContext ctx{graph, camera, lighting, vp, vp.inverted(), eye, size};

                auto render = [&](const WaterRenderer& renderer, bool reference, float clearDepth) {
                    require(target.bind(), "Cannot bind framebuffer for water pass");
                    gl.glDepthMask(GL_TRUE);
                    gl.glClearDepth(clearDepth);
                    gl.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                    renderer.render(ctx, resources);
                    if (reference) {
                        // WaterRenderer draws the first three vertices. Complete the independent quad.
                        gl.glUseProgram(quadProgram.programId());
                        gl.glBindVertexArray(vao);
                        gl.glDisable(GL_CULL_FACE);
                        gl.glEnable(GL_DEPTH_TEST);
                        gl.glDepthFunc(GL_LEQUAL);
                        gl.glEnable(GL_BLEND);
                        gl.glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
                        gl.glDrawArrays(GL_TRIANGLES, 3, 3);
                    }
                    Pixels result{std::vector<unsigned char>(pixelCount * 4), std::vector<float>(pixelCount)};
                    QOpenGLFramebufferObject::blitFramebuffer(&resolved, &target,
                        GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
                    require(resolved.bind(), "Cannot bind resolve framebuffer");
                    gl.glReadPixels(0, 0, size.width(), size.height(), GL_RGBA, GL_UNSIGNED_BYTE, result.color.data());
                    gl.glReadPixels(0, 0, size.width(), size.height(), GL_DEPTH_COMPONENT, GL_FLOAT, result.depth.data());
                    require(gl.glGetError() == GL_NO_ERROR, "OpenGL error during water pass");
                    require(target.bind(), "Cannot restore test framebuffer");
                    return result;
                };
                const auto actual = render(triangle, false, 1.0f);
                const auto expected = render(quad, true, 1.0f);
                for (size_t p = 0; p < pixelCount; ++p) {
                    require(std::isfinite(actual.depth[p]) && std::abs(actual.depth[p] - expected.depth[p]) < 2e-6f,
                        "Triangle/quad water depth differs");
                    for (int c = 0; c < 4; ++c)
                        require(std::abs(int(actual.color[p * 4 + c]) - int(expected.color[p * 4 + c])) <= 1,
                            "Triangle/quad water color differs");
                    if (actual.depth[p] >= 1.0f) continue;
                    ++totalHits;
                    const float x = (float(p % size.width()) + 0.5f) / size.width() * 2 - 1;
                    const float y = (float(p / size.width()) + 0.5f) / size.height() * 2 - 1;
                    const auto world = ctx.invViewProjection * QVector4D(x, y, actual.depth[p] * 2 - 1, 1);
                    const QVector3D hit = world.toVector3DAffine();
                    if (hit.length() < seaRadius - 1e-4f) ++troughHits;
                    const auto ray = (hit - eye).normalized();
                    const float b = QVector3D::dotProduct(eye, ray);
                    if (b * b - (eye.lengthSquared() - seaRadius * seaRadius) < -1e-5f) ++beyondMeanHorizon;
                }
                if (view == 1) {
                    target.toImage().save("build/Planet/water-screen-pass.png");
                }
                // An opaque foreground depth must prevent writes from the screen primitive.
                const auto occluded = render(triangle, false, 0.0f);
                require(std::all_of(occluded.depth.begin(), occluded.depth.end(), [](float d) { return d == 0; }),
                    "Water overwrote foreground depth");
                require(std::all_of(occluded.color.begin(), occluded.color.end(), [](unsigned char c) { return c == 0; }),
                    "Water painted over foreground");

                const auto originalWater = scene.waterParams();
                auto flatWater = originalWater;
                flatWater.wavesEnabled = false;
                require(scene.setWaterParams(flatWater) == WaterUpdateKind::OpticsOnly,
                    "Disabling waves should not rebuild terrain or wave geometry");
                const auto flat = render(triangle, false, 1.0f);
                size_t flatHits = 0;
                for (size_t p = 0; p < pixelCount; ++p) {
                    const float x = (float(p % size.width()) + 0.5f) / size.width() * 2 - 1;
                    const float y = (float(p / size.width()) + 0.5f) / size.height() * 2 - 1;
                    const auto farPoint = (ctx.invViewProjection * QVector4D(x, y, 1, 1)).toVector3DAffine();
                    const auto ray = (farPoint - eye).normalized();
                    const float b = QVector3D::dotProduct(eye, ray);
                    const float discriminant = b * b - (eye.lengthSquared() - seaRadius * seaRadius);
                    if (std::abs(discriminant) < 1e-5f) continue;
                    if (discriminant < 0) {
                        require(flat.depth[p] == 1.0f, "Flat water extended beyond the mean sea sphere");
                    } else {
                        const float t = -b - std::sqrt(discriminant);
                        const auto clip = vp * QVector4D(eye + ray * t, 1);
                        const float expectedDepth = clip.z() / clip.w() * 0.5f + 0.5f;
                        require(flat.depth[p] < 1 && std::abs(flat.depth[p] - expectedDepth) < 2e-6f,
                            "Flat water depth differs from analytic sea sphere");
                        ++flatHits;
                    }
                }
                require(flatHits > 0, "Flat water disappeared");
                // Time, wave shape and reflection controls must have no effect in flat mode.
                lighting.waterTime += 123.0;
                flatWater.shellWaveAmplitude = 0.05f;
                flatWater.shellWaveFrequency = 1.0f;
                flatWater.fresnelStrength = 2.5f;
                flatWater.reflectionStrength = 0.0f;
                flatWater.specularIntensity = 3.0f;
                flatWater.glintIntensity = 4.0f;
                flatWater.foamIntensity = 3.0f;
                scene.setWaterParams(flatWater);
                const auto flatChanged = render(triangle, false, 1.0f);
                require(flat.color == flatChanged.color && flat.depth == flatChanged.depth,
                    "Flat water still depends on waves, foam or reflections");
                if (view == 1) target.toImage().save("build/Planet/water-flat-pass.png");
                const auto flatOccluded = render(triangle, false, 0.0f);
                require(std::all_of(flatOccluded.color.begin(), flatOccluded.color.end(), [](unsigned char c) { return c == 0; }),
                    "Flat water painted over foreground");
                scene.setWaterParams(originalWater);
                lighting.waterTime -= 123.0;
                const auto restored = render(triangle, false, 1.0f);
                require(actual.color == restored.color && actual.depth == restored.depth,
                    "Re-enabling waves did not restore the original water");
            }
            require(totalHits > 0 && troughHits > 0 && beyondMeanHorizon > 0,
                "Test did not exercise water, troughs and crests beyond the mean horizon");
            qInfo() << "Water screen pass passed; MSAA:" << samples << "; hits/troughs/beyond mean horizon:"
                << totalHits << troughHits << beyondMeanHorizon;
        }
        const GLuint textures[]{depthTexture, environment, radius, kind, shore};
        gl.glDeleteTextures(5, textures);
        gl.glDeleteVertexArrays(1, &vao);
        return true;
    } catch (const std::exception& error) {
        qCritical() << "Water screen pass failed:" << error.what();
        return false;
    }
}

bool runWaterScreenPassBenchmark(const QString& outputPrefix) {
    try {
        QOpenGLContext context;
        context.setFormat(QSurfaceFormat::defaultFormat());
        require(context.create(), "Cannot create benchmark OpenGL context");
        QOffscreenSurface surface;
        surface.setFormat(context.format());
        surface.create();
        require(context.makeCurrent(&surface), "Cannot make benchmark context current");
        QOpenGLFunctions_3_3_Core gl;
        require(gl.initializeOpenGLFunctions(), "OpenGL 3.3 unavailable");
        qInfo() << "Water benchmark GPU:" << reinterpret_cast<const char*>(gl.glGetString(GL_RENDERER));

        QFile timingFile(outputPrefix + "-timing.csv"), memoryFile(outputPrefix + "-memory.csv");
        require(timingFile.open(QIODevice::WriteOnly | QIODevice::Text)
            && memoryFile.open(QIODevice::WriteOnly | QIODevice::Text), "Cannot open benchmark CSVs");
        QTextStream timing(&timingFile), memory(&memoryFile);
        timing.setRealNumberPrecision(10);
        memory.setRealNumberPrecision(10);
        timing << "level,view,width,height,msaa,iteration,variant,gpu_ms\n";
        memory << "level,triangles,payload_bytes,capacity_bytes,build_ms,upload_ms,private_before,private_after,working_set_before,working_set_after\n";

        const auto fragment = shaderSource(":/Planet/resources/water/water_shell.frag");
        QOpenGLShaderProgram screenProgram, meshProgram;
        compile(screenProgram, shaderSource(":/Planet/resources/water/water_shell.vert"), fragment);
        // Original vertex shader, before replacing the coverage mesh.
        compile(meshProgram, R"(#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 uMVP;
uniform float uWaterShellOuterRadius;
void main() {
    vec3 proxyDir = normalize(aPos);
    gl_Position = uMVP * vec4(proxyDir * uWaterShellOuterRadius, 1.0);
})", fragment);
        WaterRenderer screenRenderer(&gl, screenProgram.programId());
        WaterRenderer meshRenderer(&gl, meshProgram.programId());
        GLuint screenVao = 0, meshVao = 0, buffers[2]{}, query = 0, depthTexture = 0;
        gl.glGenVertexArrays(1, &screenVao);
        gl.glGenVertexArrays(1, &meshVao);
        gl.glGenBuffers(2, buffers);
        gl.glGenQueries(1, &query);
        gl.glGenTextures(1, &depthTexture);
        gl.glBindTexture(GL_TEXTURE_2D, depthTexture);
        const float backgroundDepth = 1;
        gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, 1, 1, 0, GL_RED, GL_FLOAT, &backgroundDepth);
        gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        const GLuint environment = constantCube(gl, 0.4f);
        const GLuint radius = constantCube(gl, 0.9f);
        const GLuint kind = constantCube(gl, 3);
        const GLuint shore = constantCube(gl, 1);
        WaterRenderer::Resources resources{environment, depthTexture, radius, kind, shore, screenVao};
        const QSize size(1280, 800);
        QOpenGLFramebufferObjectFormat format;
        format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        format.setSamples(4);
        QOpenGLFramebufferObject target(size, format);
        require(target.isValid() && target.format().samples() >= 4, "MSAA 4x unavailable");
        require(target.bind(), "Cannot bind benchmark framebuffer");
        gl.glViewport(0, 0, size.width(), size.height());
        gl.glClearColor(0, 0, 0, 0);
        gl.glClearDepth(1);
        gl.glFrontFace(GL_CCW);

        HexSphereSceneController scene;
        ecs::ComponentStorage entities;
        auto water = scene.waterParams();
        water.preset = WaterPreset::Storm;
        scene.setWaterParams(water);
        auto processMemory = []() {
            PROCESS_MEMORY_COUNTERS_EX counters{};
            require(GetProcessMemoryInfo(GetCurrentProcess(),
                reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)),
                "Cannot read process memory");
            return counters;
        };

        for (int level : {2, 4, 6}) {
            scene.setSubdivisionLevel(level);
            const float seaRadius = scene.model().waterSurfaceRadius();
            const HexSphereRenderer::RenderGraph graph{scene, entities, scene.heightStep()};
            gl.glFinish();
            const auto before = processMemory();
            QElapsedTimer elapsed;
            elapsed.start();
            auto mesh = WaterMeshGenerator::buildWaterGeometry(scene.model());
            const double buildMs = elapsed.nsecsElapsed() / 1e6;
            const auto after = processMemory();
            const size_t positionBytes = mesh.positions.size() * sizeof(float);
            const size_t indexBytes = mesh.indices.size() * sizeof(uint32_t);
            const size_t capacityBytes = mesh.positions.capacity() * sizeof(float)
                + mesh.indices.capacity() * sizeof(uint32_t);
            elapsed.restart();
            gl.glBindVertexArray(meshVao);
            gl.glBindBuffer(GL_ARRAY_BUFFER, buffers[0]);
            gl.glBufferData(GL_ARRAY_BUFFER, positionBytes, mesh.positions.data(), GL_STATIC_DRAW);
            gl.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
            gl.glEnableVertexAttribArray(0);
            gl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers[1]);
            gl.glBufferData(GL_ELEMENT_ARRAY_BUFFER, indexBytes, mesh.indices.data(), GL_STATIC_DRAW);
            gl.glFinish();
            const double uploadMs = elapsed.nsecsElapsed() / 1e6;
            require(gl.glGetError() == GL_NO_ERROR, "Mesh upload failed");
            memory << level << ',' << mesh.indices.size() / 3 << ',' << positionBytes + indexBytes
                << ',' << capacityBytes << ',' << buildMs << ',' << uploadMs
                << ',' << before.PrivateUsage << ',' << after.PrivateUsage
                << ',' << before.WorkingSetSize << ',' << after.WorkingSetSize << '\n';
            memory.flush();

            for (int view = 0; view < 3; ++view) {
                const QVector3D eye = view == 0 ? QVector3D(0, 0, seaRadius * 3)
                    : view == 1 ? QVector3D(0, 0, seaRadius * 1.5f)
                    : QVector3D(0, seaRadius * 1.12f, 0);
                const QVector3D aim = view < 2 ? QVector3D(0, 0, 0)
                    : QVector3D(0, seaRadius * 0.85f, -seaRadius);
                HexSphereRenderer::RenderCamera camera;
                camera.view.lookAt(eye, aim, view < 2 ? QVector3D(0, 1, 0) : QVector3D(0, 0, -1));
                camera.projection.perspective(45, float(size.width()) / size.height(), 0.001f, 20.0f);
                const auto vp = camera.projection * camera.view;
                const HexSphereRenderer::SceneLighting lighting{QVector3D(1, 1, 1).normalized(), 17.0};
                const HexSphereRenderer::RenderContext ctx{graph, camera, lighting, vp, vp.inverted(), eye, size};
                // Identical current wave/optics uniforms, set outside the timed draw.
                gl.glEnable(GL_RASTERIZER_DISCARD);
                resources.vao = screenVao;
                screenRenderer.render(ctx, resources);
                resources.vao = meshVao;
                meshRenderer.render(ctx, resources);
                meshProgram.bind();
                meshProgram.setUniformValue("uMVP", vp);
                gl.glDisable(GL_RASTERIZER_DISCARD);
                gl.glEnable(GL_DEPTH_TEST);
                gl.glDepthFunc(GL_LEQUAL);
                gl.glDepthMask(GL_TRUE);
                gl.glEnable(GL_BLEND);
                gl.glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

                // Alternate order, with warmup for both variants; GPU elapsed queries
                // measure execution, unlike the application's CPU submission timer.
                for (int iteration = -6; iteration < 21; ++iteration) {
                    for (int order = 0; order < 2; ++order) {
                        const bool old = ((iteration + 6 + order) % 2) == 0;
                        gl.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                        gl.glUseProgram(old ? meshProgram.programId() : screenProgram.programId());
                        gl.glBindVertexArray(old ? meshVao : screenVao);
                        if (old) { gl.glEnable(GL_CULL_FACE); gl.glCullFace(GL_BACK); }
                        else gl.glDisable(GL_CULL_FACE);
                        gl.glBeginQuery(GL_TIME_ELAPSED, query);
                        if (old) gl.glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indices.size()), GL_UNSIGNED_INT, nullptr);
                        else gl.glDrawArrays(GL_TRIANGLES, 0, 3);
                        gl.glEndQuery(GL_TIME_ELAPSED);
                        GLuint64 ns = 0;
                        gl.glGetQueryObjectui64v(query, GL_QUERY_RESULT, &ns);
                        require(gl.glGetError() == GL_NO_ERROR, "GPU timing failed");
                        if (iteration >= 0) timing << level << ',' << view << ",1280,800,4," << iteration
                            << ',' << (old ? "mesh" : "screen") << ',' << double(ns) / 1e6 << '\n';
                    }
                }
                timing.flush();
                qInfo() << "Water benchmark completed level/view" << level << view;
            }
        }
        gl.glDeleteBuffers(2, buffers);
        gl.glDeleteVertexArrays(1, &screenVao);
        gl.glDeleteVertexArrays(1, &meshVao);
        gl.glDeleteQueries(1, &query);
        const GLuint textures[]{depthTexture, environment, radius, kind, shore};
        gl.glDeleteTextures(5, textures);
        return true;
    } catch (const std::exception& error) {
        qCritical() << "Water benchmark failed:" << error.what();
        return false;
    }
}
