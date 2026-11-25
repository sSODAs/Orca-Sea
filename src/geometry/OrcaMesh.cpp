#include "OrcaMesh.h"
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    constexpr float PI = 3.14159265358979323846f;
    struct Vertex
    {
        glm::vec3 position, normal;
        // (cos(theta), sin(theta)) on the body cross-section.
        // Fins and poles keep the default zero coordinates.
        glm::vec2 bodyRadial{0.0f};
        float bodyMask = 0.0f;
    };
    struct Knot
    {
        float t, x, y, z, a, b;
    };
    using Member = float Knot::*;
    using Index = std::uint32_t;

    // Body: tail to head. a = vertical radius, b = lateral radius.
    const std::vector<Knot> BODY = {
        {-4.65f, -4.65f, 0.00f, 0, 0.00f, 0.00f},
        {-4.48f, -4.48f, 0.00f, 0, 0.10f, 0.16f},
        {-4.15f, -4.15f, 0.00f, 0, 0.13f, 0.22f},
        {-3.75f, -3.75f, -0.01f, 0, 0.18f, 0.27f},
        {-3.25f, -3.25f, -0.02f, 0, 0.26f, 0.34f},
        {-2.65f, -2.65f, -0.03f, 0, 0.39f, 0.47f},
        {-2.05f, -2.05f, -0.03f, 0, 0.53f, 0.62f},
        {-1.40f, -1.40f, -0.01f, 0, 0.67f, 0.76f},
        {-0.70f, -0.70f, 0.02f, 0, 0.79f, 0.88f},
        {1.50f, 1.50f, 0.04f, 0, 0.79f, 0.89f},
        {2.10f, 2.10f, 0.02f, 0, 0.70f, 0.79f},
        {2.65f, 2.65f, -0.02f, 0, 0.58f, 0.66f},
        {3.05f, 3.05f, -0.06f, 0, 0.47f, 0.59f},
        {3.38f, 3.38f, -0.09f, 0, 0.32f, 0.46f},
        {3.58f, 3.58f, -0.10f, 0, 0.15f, 0.26f},
        {3.65f, 3.65f, -0.11f, 0, 0.00f, 0.00f}};

        // Dorsal: a = longitudinal half-chord, b = half-thickness.
        const std::vector<Knot>
            DORSAL = {
                {0.00f, 0.20f, 0.40f, 0, 0.00f, 0.00f},
                {0.12f, 0.22f, 0.67f, 0, 0.90f, 0.13f},
                {0.26f, 0.16f, 0.99f, 0, 0.82f, 0.12f},
                {0.45f, 0.04f, 1.40f, 0, 0.59f, 0.09f},
                {0.65f, -0.14f, 1.82f, 0, 0.37f, 0.06f},
                {0.85f, -0.36f, 2.20f, 0, 0.19f, 0.035f},
                {1.00f, -0.52f, 2.43f, 0, 0.00f, 0.00f}};

    // Right pectoral fin. Mirrored for left. a = chord, b = thickness.
    const std::vector<Knot> PECTORAL = {
        {0.00f, 1.30f, -0.25f, 0.25f, 0.00f, 0.00f},
        {0.12f, 1.20f, -0.35f, 0.62f, 0.60f, 0.14f},
        {0.30f, 0.93f, -0.48f, 1.02f, 0.72f, 0.12f},
        {0.50f, 0.66f, -0.63f, 1.38f, 0.75f, 0.09f},
        {0.72f, 0.39f, -0.73f, 1.70f, 0.61f, 0.06f},
        {0.89f, 0.18f, -0.80f, 1.93f, 0.38f, 0.035f},
        {1.00f, 0.02f, -0.82f, 2.02f, 0.00f, 0.00f}};

    // Horizontal tail flukes. Two symmetric lobes with swept tips.
    const std::vector<Knot> FLUKE = {
        {0.00f, -4.27f, 0.00f, 0.00f, 0.00f, 0.00f},
        {0.10f, -4.34f, 0.00f, 0.17f, 0.53f, 0.10f},
        {0.28f, -4.43f, 0.02f, 0.48f, 0.65f, 0.095f},
        {0.50f, -4.57f, 0.04f, 0.90f, 0.57f, 0.07f},
        {0.72f, -4.75f, 0.06f, 1.34f, 0.38f, 0.05f},
        {0.90f, -4.92f, 0.08f, 1.64f, 0.18f, 0.025f},
        {1.00f, -5.08f, 0.08f, 1.78f, 0.00f, 0.00f}};

    float slope(const std::vector<Knot> &k, std::size_t i, Member m)
    {
        auto secant = [&](std::size_t j)
        {
            return (k[j + 1].*m - k[j].*m) / (k[j + 1].t - k[j].t);
        };
        if (i == 0)
            return secant(0);
        if (i + 1 == k.size())
            return secant(i - 1);
        float d0 = secant(i - 1), d1 = secant(i);
        if (d0 * d1 <= 0.0f)
            return 0.0f;
        float h0 = k[i].t - k[i - 1].t, h1 = k[i + 1].t - k[i].t;
        float w0 = 2 * h1 + h0, w1 = h1 + 2 * h0;
        return (w0 + w1) / (w0 / d0 + w1 / d1);
    }

    float sample(const std::vector<Knot> &k, float t, Member m)
    {
        if (t <= k.front().t)
            return k.front().*m;
        if (t >= k.back().t)
            return k.back().*m;
        std::size_t i = 0;
        while (i + 1 < k.size() - 1 && t > k[i + 1].t)
            ++i;
        float h = k[i + 1].t - k[i].t, u = (t - k[i].t) / h;
        float u2 = u * u, u3 = u2 * u;
        return (2 * u3 - 3 * u2 + 1) * (k[i].*m) + (u3 - 2 * u2 + u) * h * slope(k, i, m) + (-2 * u3 + 3 * u2) * (k[i + 1].*m) + (u3 - u2) * h * slope(k, i + 1, m);
    }

    glm::vec3 center(const std::vector<Knot> &k, float t)
    {
        return {sample(k, t, &Knot::x), sample(k, t, &Knot::y), sample(k, t, &Knot::z)};
    }

    // Each loft is a closed, individually manifold surface; fin roots overlap body.
    // axisA cross axisB points along the direction of the loft.
    void appendLoft(std::vector<Vertex> &v, std::vector<Index> &idx,
                    const std::vector<Knot> &k, int segments, int radial,
                    glm::vec3 axisA, glm::vec3 axisB)
    {
        Index base = static_cast<Index>(v.size());
        v.push_back({center(k, k.front().t), glm::vec3(0)});
        for (int i = 1; i < segments; ++i)
        {
            float t = k.front().t + (k.back().t - k.front().t) * float(i) / float(segments);
            glm::vec3 c = center(k, t);
            float a = sample(k, t, &Knot::a), b = sample(k, t, &Knot::b);
            if (a <= 0 || b <= 0)
                throw std::runtime_error("Invalid loft radius");
            for (int j = 0; j < radial; ++j)
            {
                float theta = 2 * PI * float(j) / float(radial);
                v.push_back({c + a * std::cos(theta) * axisA + b * std::sin(theta) * axisB, glm::vec3(0)});
            }
        }
        Index tip = static_cast<Index>(v.size());
        v.push_back({center(k, k.back().t), glm::vec3(0)});
        auto ring = [&](int i, int j)
        { return base + 1 + static_cast<Index>(i * radial + j % radial); };
        for (int j = 0; j < radial; ++j)
            idx.insert(idx.end(), {base, ring(0, j + 1), ring(0, j)});
        for (int i = 0; i < segments - 2; ++i)
            for (int j = 0; j < radial; ++j)
            {
                Index a = ring(i, j), b = ring(i, j + 1), c = ring(i + 1, j), d = ring(i + 1, j + 1);
                idx.insert(idx.end(), {a, b, c, b, d, c});
            }
        for (int j = 0; j < radial; ++j)
            idx.insert(idx.end(), {ring(segments - 2, j), ring(segments - 2, j + 1), tip});
    }

    void appendPair(std::vector<Vertex> &v, std::vector<Index> &idx,
                    const std::vector<Knot> &source, int segments, int radial)
    {
        for (float side : {-1.0f, 1.0f})
        {
            auto k = source;
            for (auto &p : k)
                p.z *= side;
            appendLoft(v, idx, k, segments, radial, {1, 0, 0}, {0, side, 0});
        }
    }

    // Material is evaluated in rest-pose object coordinates. This keeps the
    // markings attached to the body when the mesh is animated later.
    const char *vsSource = R"GLSL(#version 330 core
layout(location=0) in vec3 aPosition;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aBodyRadial;
layout(location=3) in float aBodyMask;
uniform mat4 uModel;
uniform mat4 uViewProjection;
uniform mat4 uBones[9];
uniform float uRootX;
uniform float uBoneStep;
uniform float uLengthScale;
out vec3 vNormal;
out vec3 vLocalPosition;
out vec2 vBodyRadial;
flat out float vBodyMask;
void main(){
    // Adjacent bone transforms are blended using the rest-pose X coordinate.
    // All six loft components use the same field, including overlapping fin roots.
    float q=clamp((uRootX-aPosition.x)/uBoneStep,0.0,8.0);
    int i=min(int(floor(q)),7);
    float t=q-float(i);
    float w=t*t*(3.0-2.0*t);
    mat4 a=uBones[i], b=uBones[i+1];
    vec4 p0=a*vec4(aPosition,1.0), p1=b*vec4(aPosition,1.0);
    vec4 posed=mix(p0,p1,w);
    // Jacobian of the blend, including variation in weights along X.
    // Inverse transpose corrects the normal after non-rigid deformation.
    mat3 J=(1.0-w)*mat3(a)+w*mat3(b);
    float dw=-6.0*t*(1.0-t)/uBoneStep;
    J[0]+=(p1.xyz-p0.xyz)*dw;

    // Head length protection
    const float HEAD_STRETCH_RATIO = 0.10;
    const float TRANSITION_LENGTH = 0.45;

    float d = max(aPosition.x - uRootX, 0.0);

    float blend = clamp(
        d / TRANSITION_LENGTH,
        0.0, 1.0
    );

    // Smooth derivative across the transition.
    float s = blend * blend * (3.0 - 2.0 * blend);

    // Integrated smoothstep for continuous deformation.
    float integrated =
        TRANSITION_LENGTH *
        (pow(blend, 3.0) - 0.5 * pow(blend, 4.0))
        + max(d - TRANSITION_LENGTH, 0.0);

    float headScale =
        1.0 + (uLengthScale - 1.0) * HEAD_STRETCH_RATIO;

    float correction =
        headScale / uLengthScale - 1.0;

    // Compensate for the global longitudinal scaling.
    posed.x += correction * integrated;

    // Correct surface normals using the deformation Jacobian.
    J[0].x += correction * s;

    vNormal = transpose(
        inverse(mat3(uModel) * J)
    ) * aNormal;
        vLocalPosition=aPosition;
        vBodyRadial=aBodyRadial;
        vBodyMask=aBodyMask;
        gl_Position=uViewProjection*uModel*posed;
    }
    )GLSL";
    const char *fsSource = R"GLSL(#version 330 core
    in vec3 vNormal;
    in vec3 vLocalPosition;
    in vec2 vBodyRadial;
    flat in float vBodyMask;
    uniform bool uWireframe;
    uniform float uAgentOpacity;
    out vec4 FragColor;

    // Anti-aliased elliptical markings in (longitudinal, angular) coordinates.
    float ellipseMask(vec2 p, vec2 radius){
        float d=length(p/radius);
        float aa=clamp(fwidth(d),0.012,0.12);
        return 1.0-smoothstep(1.0-aa,1.0+aa,d);
    }

    void main(){
        if(uWireframe){FragColor=vec4(0.40,0.80,1.0,.60+.35*uAgentOpacity);return;}

        // Charcoal instead of absolute black for readable silhouettes.
        vec3 ink=vec3(0.115,0.135,0.155);
        vec3 white=vec3(0.88,0.90,0.91);
        vec3 saddleGray=vec3(0.33,0.37,0.41);
        vec3 base=ink;

        // theta = 0 at the back, pi/2 at the flank, pi at the belly.
        // abs() mirrors the eye and flank markings to both sides.
        float x=vLocalPosition.x;
        float theta=atan(abs(vBodyRadial.y)+0.00001,vBodyRadial.x);
        // A small sweep keeps the oval from looking mechanically stamped.
        float eyeCenter=0.94+0.13*(x-2.20);
        float eye=ellipseMask(vec2(x-2.20,theta-eyeCenter),vec2(0.78,0.43));
        // Saddle is immediately behind the dorsal fin (toward negative X).
        float saddleCenter=-1.24-0.22*pow(clamp(theta/1.2,0.0,1.0),2.0);
        float saddle=ellipseMask(vec2(x-saddleCenter,theta),vec2(0.73,0.95));
        // Ventral white region, tapering before the tail and under the jaw.
        float bellySpan=smoothstep(-3.95,-3.10,x)*(1.0-smoothstep(3.40,3.63,x));
        float bellyEdge=2.35-0.20*exp(-pow((x-2.65)/0.8,2.0));
        float belly=bellySpan*smoothstep(bellyEdge-0.04,bellyEdge+0.04,theta);
        // Rear flank blaze connects visually to the ventral marking.
        float flankCenter=2.20+0.12*(x+2.00);
        float flank=ellipseMask(vec2(x+2.00,theta-flankCenter),vec2(0.84,0.68));
        float bodyWhite=max(eye,max(belly,flank));
        base=mix(base,saddleGray,saddle*0.90*vBodyMask);
        base=mix(base,white,bodyWhite*vBodyMask);

        // Broad overhead illumination with a weaker fill from the opposite side.
        vec3 n=normalize(vNormal);
        float key=max(dot(n,normalize(vec3(0.25,1.0,0.35))),0.0);
        float fill=max(dot(n,normalize(vec3(-0.55,0.35,-0.75))),0.0);
        float illumination=0.50+0.58*key+0.20*fill;
        vec3 lit=base*illumination;
        // Soft highlight on the dorsal surfaces, not a mirror-like reflection.
        lit+=vec3(0.025)*pow(key,12.0);
        // Internal scene buffer: alpha is a material tag, NOT display opacity.
        // 0 = empty, <.20 water, .25-.45 fin, .60-.95 body; tag encodes depth opacity.
        FragColor=vec4(clamp(lit,0.0,1.0),mix(.25+.20*uAgentOpacity,.60+.35*uAgentOpacity,vBodyMask));
    }
    )GLSL";
    GLuint compileShader(GLenum type, const char *source)
    {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &source, nullptr);
        glCompileShader(s);
        GLint ok = GL_FALSE;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            char log[2048]{};
            glGetShaderInfoLog(s, sizeof(log), nullptr, log);
            glDeleteShader(s);
            throw std::runtime_error(log);
        }
        return s;
    }
}

OrcaMesh::OrcaMesh()
{
    createShader();
    try
    {
        generateBody();
    }
    catch (...)
    {
        glDeleteProgram(shader);
        throw;
    }
}
OrcaMesh::~OrcaMesh()
{
    glDeleteBuffers(1, &ebo);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(shader);
}
void OrcaMesh::createShader()
{
    GLuint vs = compileShader(GL_VERTEX_SHADER, vsSource), fs = 0;
    try
    {
        fs = compileShader(GL_FRAGMENT_SHADER, fsSource);
    }
    catch (...)
    {
        glDeleteShader(vs);
        throw;
    }
    shader = glCreateProgram();
    glAttachShader(shader, vs);
    glAttachShader(shader, fs);
    glLinkProgram(shader);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = GL_FALSE;
    glGetProgramiv(shader, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[2048]{};
        glGetProgramInfoLog(shader, sizeof(log), nullptr, log);
        glDeleteProgram(shader);
        shader = 0;
        throw std::runtime_error(log);
    }
}
void OrcaMesh::generateBody()
{
    std::vector<Vertex> vertices;
    std::vector<Index> indices;
    constexpr int bodySegments = 96, bodyRadial = 48;
    appendLoft(vertices, indices, BODY, bodySegments, bodyRadial, {0, 1, 0}, {0, 0, 1});
    // Keep body material coordinates separate from the fin components.
    for (auto &v : vertices)
        v.bodyMask = 1.0f;
    for (int i = 0; i < bodySegments - 1; ++i)
    {
        for (int j = 0; j < bodyRadial; ++j)
        {
            float theta = 2 * PI * float(j) / float(bodyRadial);
            vertices[1 + i * bodyRadial + j].bodyRadial = {std::cos(theta), std::sin(theta)};
        }
    }
    appendLoft(vertices, indices, DORSAL, 40, 24, {1, 0, 0}, {0, 0, -1});
    appendPair(vertices, indices, PECTORAL, 40, 24);
    appendPair(vertices, indices, FLUKE, 36, 24);
    for (std::size_t i = 0; i < indices.size(); i += 3)
    {
        auto &a = vertices[indices[i]];
        auto &b = vertices[indices[i + 1]];
        auto &c = vertices[indices[i + 2]];
        glm::vec3 n = glm::cross(b.position - a.position, c.position - a.position);
        if (glm::dot(n, n) < 1e-16f)
            throw std::runtime_error("Degenerate triangle in OrcaMesh");
        a.normal += n;
        b.normal += n;
        c.normal += n;
    }
    for (auto &v : vertices)
    {
        if (glm::dot(v.normal, v.normal) < 1e-16f)
            throw std::runtime_error("Invalid vertex normal");
        v.normal = glm::normalize(v.normal);
    }
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)), vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(Index)), indices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void *>(offsetof(Vertex, position)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void *>(offsetof(Vertex, normal)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void *>(offsetof(Vertex, bodyRadial)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void *>(offsetof(Vertex, bodyMask)));
    glEnableVertexAttribArray(3);
    glBindVertexArray(0);
    indexCount = static_cast<GLsizei>(indices.size());
    std::cout << "\n[OrcaMesh | Stage 04.1]\nVertices: " << vertices.size() << "\nTriangles: " << indices.size() / 3
              << "\nBody length: " << BODY.back().x - BODY.front().x << "\nMaterial: procedural black/white, eye patches, saddle, ventral markings\nRig: 9 joints, procedural pitch wave, GPU skinning\nMesh generation complete\n";
}
void OrcaMesh::draw(const glm::mat4 &vp, const glm::mat4 &model, const SwimRig::Pose &pose, bool wireframe, float opacity) const
{
    glUseProgram(shader);
    glUniformMatrix4fv(glGetUniformLocation(shader, "uViewProjection"), 1, GL_FALSE, glm::value_ptr(vp));
    glUniformMatrix4fv(glGetUniformLocation(shader, "uModel"), 1, GL_FALSE, glm::value_ptr(model));
    static_assert(SwimRig::Count == 9, "Update GLSL bone array size when changing the rig");
    // Upload each matrix individually: no assumptions about array memory padding.
    for (int i = 0; i < SwimRig::Count; ++i)
    {
        const std::string name = "uBones[" + std::to_string(i) + "]";
        glUniformMatrix4fv(glGetUniformLocation(shader, name.c_str()), 1, GL_FALSE, glm::value_ptr(pose.skin[i]));
    }
    glUniform1f(glGetUniformLocation(shader, "uRootX"), SwimRig::RootX);
    glUniform1f(glGetUniformLocation(shader, "uBoneStep"), SwimRig::Step);
    glUniform1f(
        glGetUniformLocation(shader, "uLengthScale"),
        SwimRig::LENGTH_SCALE);
    glUniform1i(glGetUniformLocation(shader, "uWireframe"), wireframe ? 1 : 0);
    glUniform1f(glGetUniformLocation(shader, "uAgentOpacity"), opacity);
    glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}
