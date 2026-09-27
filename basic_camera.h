#ifndef basic_camera_h
#define basic_camera_h

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class BasicCamera {
public:

    glm::vec3 eye;
    glm::vec3 lookAt;
    glm::vec3 V;

    BasicCamera(float eyeX = 0.0, float eyeY = 1.0, float eyeZ = 3.0, float lookAtX = 0.0, float lookAtY = 0.0, float lookAtZ = 0.0, glm::vec3 viewUpVector = glm::vec3(0.0f, 1.0f, 0.0f))
    {
        eye = glm::vec3(eyeX, eyeY, eyeZ);
        lookAt = glm::vec3(lookAtX, lookAtY, lookAtZ);
        V = viewUpVector;
    }

    glm::mat4 createViewMatrix()
    {
        glm::mat4 viewMatrix;

        glm::vec3 N = eye - lookAt;
        glm::vec3 U = glm::cross(V, N);
        u = glm::normalize(U);
        v = glm::normalize(glm::cross(N, U));
        n = glm::normalize(N);

        float dx = glm::dot(-eye, u);
        float dy = glm::dot(-eye, v);
        float dz = glm::dot(-eye, n);

        viewMatrix[0].x = u.x;  viewMatrix[1].x = u.y;  viewMatrix[2].x = u.z;  viewMatrix[3].x = dx;
        viewMatrix[0].y = v.x;  viewMatrix[1].y = v.y;  viewMatrix[2].y = v.z;  viewMatrix[3].y = dy;
        viewMatrix[0].z = n.x;  viewMatrix[1].z = n.y;  viewMatrix[2].z = n.z;  viewMatrix[3].z = dz;
        viewMatrix[0].w = 0.0f; viewMatrix[1].w = 0.0f; viewMatrix[2].w = 0.0f; viewMatrix[3].w = 1.0f;

        return viewMatrix;
    }

    void changeEye(float eyeX, float eyeY, float eyeZ)
    {
        eye = glm::vec3(eyeX, eyeY, eyeZ);
    }

    void changeLookAt(float lookAtX, float lookAtY, float lookAtZ)
    {
        lookAt = glm::vec3(lookAtX, lookAtY, lookAtZ);
    }

    void changeViewUpVector(glm::vec3 viewUpVector)
    {
        V = viewUpVector;
    }

    glm::vec3 get_u()
    {
        return u;
    }

    glm::vec3 get_v()
    {
        return v;
    }

    glm::vec3 get_n()
    {
        return n;
    }

private:
    glm::vec3 u;
    glm::vec3 v;
    glm::vec3 n;
};

#endif /* basic_camera_h */
#pragma once

