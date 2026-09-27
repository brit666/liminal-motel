#ifndef SPOT_LIGHT_H
#define SPOT_LIGHT_H

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include "shader.h"

class SpotLight {
public:
    glm::vec3 position;
    glm::vec3 direction;

    float cutOff;
    float outerCutOff;

    float k_c;
    float k_l;
    float k_q;

    glm::vec3 ambient;
    glm::vec3 diffuse;
    glm::vec3 specular;

    SpotLight(
        glm::vec3 pos = glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3 dir = glm::vec3(0.0f, 0.0f, -1.0f),
        float cutOffAngle = 12.5f,
        float outerCutOffAngle = 17.5f,
        float kc = 1.0f, float kl = 0.09f, float kq = 0.032f,
        glm::vec3 amb = glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3 diff = glm::vec3(1.0f, 1.0f, 0.9f),
        glm::vec3 spec = glm::vec3(1.0f, 1.0f, 1.0f)
    ) {
        position = pos;
        direction = dir;
        cutOff = glm::cos(glm::radians(cutOffAngle));
        outerCutOff = glm::cos(glm::radians(outerCutOffAngle));
        k_c = kc;
        k_l = kl;
        k_q = kq;
        ambient = amb;
        diffuse = diff;
        specular = spec;
    }

    // index selects which slot of the shader's spotLights[] array this light
    // uploads to: 0 = the camera-mounted flashlight, 1 = a room's desk lamp.
    // (See fragmentShaderForPhongShading.glsl / the Gouraud vertex shader —
    // both declare spotLights[NR_SPOT_LIGHTS].)
    void setUpSpotLight(Shader& lightingShader, int index = 0) {
        lightingShader.use();
        std::string base = "spotLights[" + std::to_string(index) + "].";
        lightingShader.setVec3(base + "position", position);
        lightingShader.setVec3(base + "direction", direction);
        lightingShader.setFloat(base + "cutOff", cutOff);
        lightingShader.setFloat(base + "outerCutOff", outerCutOff);
        lightingShader.setFloat(base + "k_c", k_c);
        lightingShader.setFloat(base + "k_l", k_l);
        lightingShader.setFloat(base + "k_q", k_q);
        lightingShader.setVec3(base + "ambient", ambient);
        lightingShader.setVec3(base + "diffuse", diffuse);
        lightingShader.setVec3(base + "specular", specular);
    }
};

#endif
