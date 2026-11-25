#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include "SwimRig.h"

class OrcaMesh {
public:
    OrcaMesh();
    ~OrcaMesh();
    OrcaMesh(const OrcaMesh&) = delete;
    OrcaMesh& operator=(const OrcaMesh&) = delete;
    void draw(const glm::mat4& viewProjection, const glm::mat4& model,
              const SwimRig::Pose& pose, bool wireframe = false, float opacity = 1.0f) const;
private:
    GLuint vao=0,vbo=0,ebo=0,shader=0;
    GLsizei indexCount=0;
    void generateBody();
    void createShader();
};
