#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "shader.h"
#include "camera.h"
#include "pointLight.h"
#include "spotLight.h"
#include "proceduralGenerator.h"
#include "sphere.h"

#include <iostream>
#include <vector>
#include <cmath>

using namespace std;

// ============================================================================
// CONSTANTS & SETTINGS
// (ROOM_WIDTH / ROOM_HEIGHT / ROOM_DEPTH / DOOR_* / WallSide all now live in
//  proceduralGenerator.h so rendering and collision can never disagree)
// ============================================================================
const unsigned int SCR_WIDTH = 1280;
const unsigned int SCR_HEIGHT = 960;

// Half-width of the player's collision footprint on the XZ plane.
const float PLAYER_RADIUS = 0.28f;

// ============================================================================
// GLOBAL STATE
// ============================================================================
Camera camera(glm::vec3(ROOM_WIDTH * 0.5f, 1.1f, -2.0f));
float lastX = SCR_WIDTH / 2.0f;
float lastY = SCR_HEIGHT / 2.0f;
bool firstMouse = true;
float deltaTime = 0.0f;
float lastFrame = 0.0f;

// Room management
std::vector<RoomCell> currentRooms;
glm::vec3 lastCameraGridPos = glm::vec3(0.0f);

// Collision boxes for every wall/door/furniture piece in the currently
// loaded 3x3 room grid. Rebuilt once per frame; processInput() tests
// candidate moves against this so the player can never clip through
// anything solid.
std::vector<CollisionBox2D> currentCollisionBoxes;

// Shading model toggle (Requirement 4: support both Phong and Gouraud)
bool useGouraudShading = false;
bool flashlightOn = true;

// ============================================================================
// FUNCTION DECLARATIONS
// ============================================================================
void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void processInput(GLFWwindow* window);

std::vector<CollisionBox2D> BuildCollisionBoxes(const std::vector<RoomCell>& rooms);
bool CollidesAt(float x, float z, const std::vector<CollisionBox2D>& boxes);

void renderCube(unsigned int& VAO, Shader& shader, const glm::mat4& model,
    const Material& material);
void renderRoom(unsigned int& cubeVAO, Shader& shader,
    const RoomCell& room, float currentTime, Sphere& lampBulb,
    const glm::vec3 lampPositions[4]);
void renderBed(unsigned int& cubeVAO, Shader& shader, const RoomCell& room);
void renderDesk(unsigned int& cubeVAO, Shader& shader, const RoomCell& room, Sphere& lampBulb);
void renderSofa(unsigned int& cubeVAO, Shader& shader, const RoomCell& room);
void renderTV(unsigned int& cubeVAO, Shader& shader, const RoomCell& room);
void renderFireplace(unsigned int& cubeVAO, Shader& shader, const RoomCell& room);
void renderCeilingFan(unsigned int& cubeVAO, Shader& shader, glm::vec3 roomPos,
    float currentTime, Sphere& lampBulb);
void renderWall(unsigned int& cubeVAO, Shader& shader, const RoomCell& room, int wallSide);
void setupCube(unsigned int& VAO, unsigned int& VBO, unsigned int& EBO);

// ============================================================================
// MAIN PROGRAM
// ============================================================================
int main()
{
    if (!glfwInit()) {
        cerr << "Failed to initialize GLFW" << endl;
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT,
        "Procedural Infinite Hotel", NULL, NULL);
    if (window == NULL) {
        cerr << "Failed to create GLFW window" << endl;
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetKeyCallback(window, key_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        cerr << "Failed to initialize GLAD" << endl;
        return -1;
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CW);

    // ========================================================================
    // SHADER SETUP
    // ========================================================================
    Shader lightingShader("vertexShaderForPhongShading.vs",
        "fragmentShaderForPhongShading.glsl");
    Shader gouraudShader("vertexShaderForGouraudShading.vs",
        "fragmentShaderForGouraudShading.fs");

    Sphere lampBulb(0.12f, 20, 10,
        glm::vec3(0.95f, 0.9f, 0.6f),
        glm::vec3(0.95f, 0.9f, 0.6f),
        glm::vec3(1.0f, 1.0f, 1.0f),
        4.0f);

    unsigned int cubeVAO, cubVBO, cubeEBO;
    setupCube(cubeVAO, cubVBO, cubeEBO);

    // ========================================================================
    // LIGHTING SETUP
    // ========================================================================
    PointLight pointLights[4] = {
        PointLight(1.5f, 2.8f, 0.5f, 0.22f, 0.22f, 0.22f,
                   0.8f, 0.8f, 0.8f, 1.0f, 1.0f, 1.0f, 1.0f, 0.09f, 0.032f, 1),
        PointLight(-1.5f, 2.8f, 0.5f, 0.22f, 0.22f, 0.22f,
                   0.8f, 0.8f, 0.8f, 1.0f, 1.0f, 1.0f, 1.0f, 0.09f, 0.032f, 2),
        PointLight(1.5f, 2.8f, -0.5f, 0.22f, 0.22f, 0.22f,
                   0.8f, 0.8f, 0.8f, 1.0f, 1.0f, 1.0f, 1.0f, 0.09f, 0.032f, 3),
        PointLight(-1.5f, 2.8f, -0.5f, 0.22f, 0.22f, 0.22f,
                   0.8f, 0.8f, 0.8f, 1.0f, 1.0f, 1.0f, 1.0f, 0.09f, 0.032f, 4)
    };

    SpotLight flashlight(
        camera.Position, camera.Front,
        12.5f, 17.5f,
        1.0f, 0.09f, 0.032f,
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, 1.0f, 0.9f),
        glm::vec3(1.0f, 1.0f, 1.0f)
    );

    // Desk lamp — repositioned/re-aimed per room, switched fully off (zero
    // ambient/diffuse/specular) for rooms with no desk.
    SpotLight deskLampLight(
        glm::vec3(0.0f), glm::vec3(0.0f, -1.0f, 0.0f),
        30.0f, 42.0f,
        1.0f, 0.18f, 0.1f,
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, 0.85f, 0.55f),
        glm::vec3(0.6f, 0.55f, 0.4f)
    );

    cout << "Controls: WASD = move, mouse = look, scroll = zoom, "
        << "G = toggle Phong/Gouraud shading, F = toggle flashlight, ESC = quit" << endl;

    currentRooms = ProceduralGenerator::GenerateRoomGrid(camera.Position);
    lastCameraGridPos = glm::vec3(
        floor(camera.Position.x / ROOM_WIDTH), 0.0f, floor(camera.Position.z / ROOM_DEPTH));
    currentCollisionBoxes = BuildCollisionBoxes(currentRooms);

    // ========================================================================
    // RENDER LOOP
    // ========================================================================
    while (!glfwWindowShouldClose(window)) {
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        processInput(window);

        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        Shader& activeShader = useGouraudShading ? gouraudShader : lightingShader;
        activeShader.use();

        glm::mat4 projection = glm::perspective(
            glm::radians(camera.Zoom), (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.1f, 150.0f);
        glm::mat4 view = camera.GetViewMatrix();

        activeShader.setMat4("projection", projection);
        activeShader.setMat4("view", view);
        activeShader.setVec3("viewPos", camera.Position);

        flashlight.position = camera.Position;
        flashlight.direction = camera.Front;
        if (flashlightOn) {
            flashlight.diffuse = glm::vec3(1.0f, 1.0f, 0.9f);
            flashlight.specular = glm::vec3(1.0f, 1.0f, 1.0f);
        }
        else {
            flashlight.diffuse = glm::vec3(0.0f);
            flashlight.specular = glm::vec3(0.0f);
        }
        flashlight.setUpSpotLight(activeShader, 0);

        glm::vec3 currentCameraGridPos(
            floor(camera.Position.x / ROOM_WIDTH), 0.0f, floor(camera.Position.z / ROOM_DEPTH));

        if (glm::distance(currentCameraGridPos, lastCameraGridPos) > 0.5f) {
            currentRooms = ProceduralGenerator::GenerateRoomGrid(camera.Position);
            lastCameraGridPos = currentCameraGridPos;
        }

        ProceduralGenerator::UpdateDoors(currentRooms, camera.Position);
        currentCollisionBoxes = BuildCollisionBoxes(currentRooms);

        for (const auto& room : currentRooms) {
            glm::vec3 lampPositions[4];
            for (int i = 0; i < 4; i++) {
                float lx = (i % 2) ? (ROOM_WIDTH - 1.2f) : 1.2f;
                float lz = (i / 2) ? (ROOM_DEPTH - 1.2f) : 1.2f;
                glm::vec3 lampPos = room.position + glm::vec3(lx, ROOM_HEIGHT - 0.3f, lz);
                lampPositions[i] = lampPos;
                pointLights[i].position = lampPos;
                pointLights[i].setUpPointLight(activeShader);
            }

            glm::vec3 deskLampPos = ProceduralGenerator::GetDeskLampAnchor(room);
            deskLampLight.position = deskLampPos;
            deskLampLight.direction = glm::normalize(glm::vec3(0.15f, -1.0f, -0.35f));
            if (room.hasDesk) {
                deskLampLight.ambient = glm::vec3(0.015f, 0.014f, 0.01f);
                deskLampLight.diffuse = glm::vec3(1.0f, 0.85f, 0.55f);
                deskLampLight.specular = glm::vec3(0.6f, 0.55f, 0.4f);
            }
            else {
                deskLampLight.ambient = glm::vec3(0.0f);
                deskLampLight.diffuse = glm::vec3(0.0f);
                deskLampLight.specular = glm::vec3(0.0f);
            }
            deskLampLight.setUpSpotLight(activeShader, 1);

            renderRoom(cubeVAO, activeShader, room, currentFrame, lampBulb, lampPositions);

            if (room.hasDesk)
                renderDesk(cubeVAO, activeShader, room, lampBulb);
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteVertexArrays(1, &cubeVAO);
    glfwTerminate();
    return 0;
}

// ============================================================================
// COLLISION HELPERS
// ============================================================================
std::vector<CollisionBox2D> BuildCollisionBoxes(const std::vector<RoomCell>& rooms)
{
    std::vector<CollisionBox2D> boxes;

    for (const auto& room : rooms) {
        auto wallBoxes = ProceduralGenerator::GetWallCollisionBoxes(room);
        boxes.insert(boxes.end(), wallBoxes.begin(), wallBoxes.end());

        auto furnitureBoxes = ProceduralGenerator::GetFurnitureCollisionBoxes(room);
        boxes.insert(boxes.end(), furnitureBoxes.begin(), furnitureBoxes.end());

        for (int ws = 0; ws < 4; ws++) {
            if (room.hasDoor[ws])
                boxes.push_back(ProceduralGenerator::GetDoorLeafBox(room, ws));
        }
    }

    return boxes;
}

bool CollidesAt(float x, float z, const std::vector<CollisionBox2D>& boxes)
{
    float minX = x - PLAYER_RADIUS, maxX = x + PLAYER_RADIUS;
    float minZ = z - PLAYER_RADIUS, maxZ = z + PLAYER_RADIUS;

    for (const auto& b : boxes) {
        if (maxX > b.minX && minX < b.maxX && maxZ > b.minZ && minZ < b.maxZ)
            return true;
    }
    return false;
}

// ============================================================================
// CALLBACK FUNCTIONS
// ============================================================================
void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    glViewport(0, 0, width, height);
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    if (key == GLFW_KEY_G && action == GLFW_PRESS) {
        useGouraudShading = !useGouraudShading;
        cout << "Shading model: " << (useGouraudShading ? "Gouraud" : "Phong") << endl;
    }

    if (key == GLFW_KEY_F && action == GLFW_PRESS) {
        flashlightOn = !flashlightOn;
        cout << "Flashlight: " << (flashlightOn ? "on" : "off") << endl;
    }
}

void mouse_callback(GLFWwindow* window, double xpos, double ypos)
{
    if (firstMouse) {
        lastX = xpos; lastY = ypos; firstMouse = false;
        return;
    }
    float xoffset = xpos - lastX;
    float yoffset = lastY - ypos;
    lastX = xpos; lastY = ypos;
    camera.ProcessMouseMovement(xoffset, yoffset);
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    camera.ProcessMouseScroll(static_cast<float>(yoffset));
}

// Movement is restricted to the horizontal (XZ) plane only: looking up/down
// never changes camera.Position.y, and every candidate move is collision
// tested (per axis, so sliding along a wall still works) before being
// applied — this is what stops the player from climbing/sinking out of the
// level or walking through walls/furniture.
void processInput(GLFWwindow* window)
{
    glm::vec3 delta(0.0f);

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        delta += camera.GetMovementDelta(FORWARD, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        delta += camera.GetMovementDelta(BACKWARD, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        delta += camera.GetMovementDelta(LEFT, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        delta += camera.GetMovementDelta(RIGHT, deltaTime);

    float candidateX = camera.Position.x + delta.x;
    if (!CollidesAt(candidateX, camera.Position.z, currentCollisionBoxes))
        camera.Position.x = candidateX;

    float candidateZ = camera.Position.z + delta.z;
    if (!CollidesAt(camera.Position.x, candidateZ, currentCollisionBoxes))
        camera.Position.z = candidateZ;
}

// ============================================================================
// GEOMETRY SETUP
// ============================================================================
void setupCube(unsigned int& VAO, unsigned int& VBO, unsigned int& EBO)
{
    float vertices[] = {
        -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
         0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
         0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
        -0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,

        -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
         0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
         0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
        -0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,

        -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,
        -0.5f,  0.5f, -0.5f, -1.0f,  0.0f,  0.0f,
        -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,
        -0.5f, -0.5f,  0.5f, -1.0f,  0.0f,  0.0f,

         0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,
         0.5f,  0.5f, -0.5f,  1.0f,  0.0f,  0.0f,
         0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,
         0.5f, -0.5f,  0.5f,  1.0f,  0.0f,  0.0f,

        -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,
         0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,
         0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,
        -0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,

        -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,
         0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,
         0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,
        -0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f
    };

    unsigned int indices[] = {
        0,  1,  2,  2,  3,  0,
        4,  6,  5,  6,  4,  7,
        8, 10,  9, 10,  8, 11,
       12, 13, 14, 14, 15, 12,
       16, 18, 17, 18, 16, 19,
       20, 21, 22, 22, 23, 20
    };

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void renderCube(unsigned int& VAO, Shader& shader, const glm::mat4& model, const Material& material)
{
    shader.use();
    shader.setMat4("model", model);
    shader.setVec3("material.ambient", material.ambient);
    shader.setVec3("material.diffuse", material.diffuse);
    shader.setVec3("material.specular", material.specular);
    shader.setFloat("material.shininess", material.shininess);

    glBindVertexArray(VAO);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
}

// ============================================================================
// ROOM SHELL: floor, 4 walls (each independently a solid wall or a door),
// ceiling, lamp fixtures.
// ============================================================================
void renderRoom(unsigned int& cubeVAO, Shader& shader,
    const RoomCell& room, float currentTime, Sphere& lampBulb,
    const glm::vec3 lampPositions[4])
{
    // FLOOR (Requirement 1: 3D Transformation - Translation + Scaling)
    glm::mat4 floorModel = glm::mat4(1.0f);
    floorModel = glm::translate(floorModel, room.position + glm::vec3(ROOM_WIDTH / 2, 0, ROOM_DEPTH / 2));
    floorModel = glm::scale(floorModel, glm::vec3(ROOM_WIDTH, 0.1f, ROOM_DEPTH));

    Material floorMaterial;
    floorMaterial.ambient = room.material.ambient * 0.8f;
    floorMaterial.diffuse = room.material.diffuse;
    floorMaterial.specular = room.material.specular;
    floorMaterial.shininess = room.material.shininess;
    renderCube(cubeVAO, shader, floorModel, floorMaterial);

    // WALLS — each of the 4 independently solid or a doorway
    for (int ws = 0; ws < 4; ws++)
        renderWall(cubeVAO, shader, room, ws);

    // CEILING
    Material ceilingMaterial;
    ceilingMaterial.ambient = room.material.ambient * 0.6f;
    ceilingMaterial.diffuse = room.material.diffuse * 0.9f;
    ceilingMaterial.specular = room.material.specular * 0.5f;
    ceilingMaterial.shininess = 16.0f;

    glm::mat4 ceilingModel = glm::mat4(1.0f);
    ceilingModel = glm::translate(ceilingModel, room.position + glm::vec3(ROOM_WIDTH / 2, ROOM_HEIGHT, ROOM_DEPTH / 2));
    ceilingModel = glm::scale(ceilingModel, glm::vec3(ROOM_WIDTH, 0.1f, ROOM_DEPTH));
    renderCube(cubeVAO, shader, ceilingModel, ceilingMaterial);

    // LAMP FIXTURES — visible geometry for the 4 point lights (Requirement 4)
    for (int i = 0; i < 4; i++) {
        glm::mat4 bulbModel = glm::mat4(1.0f);
        bulbModel = glm::translate(bulbModel, lampPositions[i]);
        lampBulb.drawSphere(shader, bulbModel);
    }

    // FURNITURE (Rule 2: Procedurally Determined, curated by archetype)
    if (room.hasBed)
        renderBed(cubeVAO, shader, room);
    if (room.hasSofa)
        renderSofa(cubeVAO, shader, room);
    if (room.hasTV)
        renderTV(cubeVAO, shader, room);
    if (room.hasFireplace)
        renderFireplace(cubeVAO, shader, room);
    // Desk (+ lamp) is rendered by the caller, after the desk-lamp spotlight
    // uniforms have been uploaded for this room.

    if (room.hasFan)
        renderCeilingFan(cubeVAO, shader, room.position, currentTime, lampBulb);
}

// ============================================================================
// GENERIC WALL — solid, or split around a doorway with a detailed sliding
// pocket door. Written once in wall-local (u, v, w) coordinates and reused
// for all 4 walls via ProceduralGenerator::GetWallTransform, so North/
// South/East/West all get identical detailing.
// ============================================================================
void renderWall(unsigned int& cubeVAO, Shader& shader, const RoomCell& room, int wallSide)
{
    glm::mat4 wallT = ProceduralGenerator::GetWallTransform(room, wallSide);
    float length = ProceduralGenerator::GetWallLength(wallSide);
    Material wallMaterial = room.material;

    auto place = [&](float u, float v, float w, float su, float sv, float sw) {
        glm::mat4 m = wallT;
        m = glm::translate(m, glm::vec3(u, v, w));
        m = glm::scale(m, glm::vec3(su, sv, sw));
        return m;
        };

    if (!room.hasDoor[wallSide]) {
        glm::mat4 m = place(length * 0.5f, ROOM_HEIGHT * 0.5f, WALL_RENDER_INSET, length, ROOM_HEIGHT, WALL_THICKNESS);
        renderCube(cubeVAO, shader, m, wallMaterial);
        return;
    }

    // Split the wall around a centered doorway gap
    float gapHalf = DOOR_WIDTH / 2.0f;
    float center = length * 0.5f;
    float leftWidth = center - gapHalf;
    float rightWidth = length - (center + gapHalf);
    float leafHeight = ROOM_HEIGHT - DOOR_HEADER_HEIGHT;

    if (leftWidth > 0.01f)
        renderCube(cubeVAO, shader, place(leftWidth * 0.5f, ROOM_HEIGHT * 0.5f, WALL_RENDER_INSET,
            leftWidth, ROOM_HEIGHT, WALL_THICKNESS), wallMaterial);
    if (rightWidth > 0.01f)
        renderCube(cubeVAO, shader, place(length - rightWidth * 0.5f, ROOM_HEIGHT * 0.5f, WALL_RENDER_INSET,
            rightWidth, ROOM_HEIGHT, WALL_THICKNESS), wallMaterial);

    // Header lintel above the doorway
    renderCube(cubeVAO, shader, place(center, leafHeight + DOOR_HEADER_HEIGHT * 0.5f, WALL_RENDER_INSET,
        DOOR_WIDTH, DOOR_HEADER_HEIGHT, WALL_THICKNESS), wallMaterial);

    // Door jambs (trim framing either side of the opening)
    Material jambMaterial;
    jambMaterial.ambient = glm::vec3(0.1f, 0.1f, 0.1f);
    jambMaterial.diffuse = glm::vec3(0.25f, 0.25f, 0.26f);
    jambMaterial.specular = glm::vec3(0.3f, 0.3f, 0.3f);
    jambMaterial.shininess = 20.0f;

    for (float side : { -1.0f, 1.0f }) {
        renderCube(cubeVAO, shader, place(center + side * (gapHalf + 0.03f), leafHeight * 0.5f, WALL_RENDER_INSET + 0.03f,
            0.06f, leafHeight, WALL_THICKNESS + 0.05f), jambMaterial);
    }

    // Overhead track the door slides along
    renderCube(cubeVAO, shader, place(center, leafHeight + 0.05f, WALL_RENDER_INSET + 0.05f,
        DOOR_WIDTH + DOOR_SLIDE_DISTANCE, 0.06f, 0.1f), jambMaterial);

    // Sliding pocket door itself (Requirement 3: Proximity Motion)
    Material doorMaterial;
    doorMaterial.ambient = glm::vec3(0.1f, 0.15f, 0.1f);
    doorMaterial.diffuse = glm::vec3(0.3f, 0.5f, 0.3f);
    doorMaterial.specular = glm::vec3(0.6f, 0.6f, 0.6f);
    doorMaterial.shininess = 128.0f;

    float slide = room.doorOpenAmount[wallSide] * DOOR_SLIDE_DISTANCE;
    float doorU = center + slide;
    float doorW = WALL_RENDER_INSET + 0.05f;

    renderCube(cubeVAO, shader, place(doorU, leafHeight * 0.5f, doorW,
        DOOR_WIDTH, leafHeight, 0.08f), doorMaterial);

    // Decorative inset panel line
    Material panelLineMaterial = doorMaterial;
    panelLineMaterial.diffuse *= 0.7f;
    renderCube(cubeVAO, shader, place(doorU, leafHeight * 0.5f, doorW + 0.045f,
        0.03f, leafHeight * 0.9f, 0.02f), panelLineMaterial);

    // Door handle
    Material handleMaterial;
    handleMaterial.ambient = glm::vec3(0.15f, 0.13f, 0.05f);
    handleMaterial.diffuse = glm::vec3(0.7f, 0.6f, 0.2f);
    handleMaterial.specular = glm::vec3(0.9f, 0.85f, 0.5f);
    handleMaterial.shininess = 80.0f;

    renderCube(cubeVAO, shader, place(doorU + DOOR_WIDTH * 0.35f, leafHeight * 0.5f, doorW + 0.08f,
        0.05f, 0.2f, 0.05f), handleMaterial);
}

// ============================================================================
// BED — quadrant-placed, with a 0/180 rotation depending on which corner it
// landed in. A nightstand sits just outside its footprint.
// ============================================================================
void renderBed(unsigned int& cubeVAO, Shader& shader, const RoomCell& room)
{
    glm::vec3 anchor = ProceduralGenerator::GetBedAnchor(room);
    float rot = ProceduralGenerator::GetBedRotationDeg(room);

    glm::mat4 base = glm::translate(glm::mat4(1.0f), anchor);
    base = glm::rotate(base, glm::radians(rot), glm::vec3(0.0f, 1.0f, 0.0f));

    Material frameMaterial;
    frameMaterial.ambient = glm::vec3(0.14f, 0.09f, 0.06f);
    frameMaterial.diffuse = glm::vec3(0.25f, 0.15f, 0.08f);
    frameMaterial.specular = glm::vec3(0.15f, 0.1f, 0.1f);
    frameMaterial.shininess = 16.0f;

    glm::mat4 frameModel = base;
    frameModel = glm::translate(frameModel, glm::vec3(0.0f, 0.2f, 0.0f));
    frameModel = glm::scale(frameModel, glm::vec3(BED_WIDTH, 0.4f, BED_DEPTH));
    renderCube(cubeVAO, shader, frameModel, frameMaterial);

    Material mattressMaterial;
    mattressMaterial.ambient = glm::vec3(0.25f, 0.24f, 0.2f);
    mattressMaterial.diffuse = glm::vec3(0.85f, 0.8f, 0.7f);
    mattressMaterial.specular = glm::vec3(0.2f, 0.2f, 0.2f);
    mattressMaterial.shininess = 8.0f;

    glm::mat4 mattressModel = base;
    mattressModel = glm::translate(mattressModel, glm::vec3(0.0f, 0.45f, 0.0f));
    mattressModel = glm::scale(mattressModel, glm::vec3(BED_WIDTH - 0.2f, 0.25f, BED_DEPTH - 0.2f));
    renderCube(cubeVAO, shader, mattressModel, mattressMaterial);

    // Two pillows, side by side near the headboard end — offsets are small
    // fixed distances (NOT scaled by BED_WIDTH), so they always sit inside
    // the mattress instead of overhanging the edge.
    Material pillowMaterial;
    pillowMaterial.ambient = glm::vec3(0.3f, 0.3f, 0.3f);
    pillowMaterial.diffuse = glm::vec3(0.95f, 0.95f, 0.9f);
    pillowMaterial.specular = glm::vec3(0.1f, 0.1f, 0.1f);
    pillowMaterial.shininess = 4.0f;

    for (float side : { -0.32f, 0.32f }) {
        glm::mat4 pillowModel = base;
        pillowModel = glm::translate(pillowModel, glm::vec3(side, 0.62f, -BED_DEPTH * 0.36f));
        pillowModel = glm::scale(pillowModel, glm::vec3(0.5f, 0.15f, 0.4f));
        renderCube(cubeVAO, shader, pillowModel, pillowMaterial);
    }

    // Folded throw blanket at the foot of the bed (accent color)
    Material blanketMaterial;
    blanketMaterial.ambient = room.material.ambient;
    blanketMaterial.diffuse = room.material.diffuse * 0.8f;
    blanketMaterial.specular = glm::vec3(0.1f, 0.1f, 0.1f);
    blanketMaterial.shininess = 6.0f;

    glm::mat4 blanketModel = base;
    blanketModel = glm::translate(blanketModel, glm::vec3(0.0f, 0.6f, BED_DEPTH * 0.32f));
    blanketModel = glm::scale(blanketModel, glm::vec3(BED_WIDTH - 0.2f, 0.12f, 0.5f));
    renderCube(cubeVAO, shader, blanketModel, blanketMaterial);

    // Headboard
    glm::mat4 headboardModel = base;
    headboardModel = glm::translate(headboardModel, glm::vec3(0.0f, 0.9f, -BED_DEPTH * 0.475f));
    headboardModel = glm::scale(headboardModel, glm::vec3(BED_WIDTH, 1.0f, 0.1f));
    renderCube(cubeVAO, shader, headboardModel, frameMaterial);

    // Nightstand beside the bed
    glm::mat4 standModel = base;
    standModel = glm::translate(standModel, glm::vec3(BED_WIDTH * 0.5f + 0.32f, 0.3f, -BED_DEPTH * 0.32f));
    standModel = glm::scale(standModel, glm::vec3(0.4f, 0.6f, 0.4f));
    renderCube(cubeVAO, shader, standModel, frameMaterial);
}

// ============================================================================
// DESK — quadrant-placed, with a lamp sitting properly ON TOP of the desktop
// surface (previously it overlapped the desktop, causing visible clipping).
// ============================================================================
void renderDesk(unsigned int& cubeVAO, Shader& shader, const RoomCell& room, Sphere& lampBulb)
{
    glm::vec3 anchor = ProceduralGenerator::GetDeskAnchor(room);
    float rot = ProceduralGenerator::GetDeskRotationDeg(room);

    glm::mat4 base = glm::translate(glm::mat4(1.0f), anchor);
    base = glm::rotate(base, glm::radians(rot), glm::vec3(0.0f, 1.0f, 0.0f));

    Material topMaterial;
    topMaterial.ambient = glm::vec3(0.15f, 0.1f, 0.05f);
    topMaterial.diffuse = glm::vec3(0.55f, 0.35f, 0.18f);
    topMaterial.specular = glm::vec3(0.25f, 0.2f, 0.15f);
    topMaterial.shininess = 32.0f;

    // Desktop surface spans [DESK_HEIGHT - 0.03, DESK_HEIGHT + 0.03]
    glm::mat4 topModel = base;
    topModel = glm::translate(topModel, glm::vec3(0.0f, DESK_HEIGHT, 0.0f));
    topModel = glm::scale(topModel, glm::vec3(DESK_WIDTH, 0.06f, DESK_DEPTH));
    renderCube(cubeVAO, shader, topModel, topMaterial);

    Material legMaterial;
    legMaterial.ambient = glm::vec3(0.16f, 0.1f, 0.06f);
    legMaterial.diffuse = glm::vec3(0.35f, 0.2f, 0.1f);
    legMaterial.specular = glm::vec3(0.15f, 0.15f, 0.15f);
    legMaterial.shininess = 16.0f;

    const float legOffsets[4][2] = {
        { DESK_WIDTH * 0.42f,  DESK_DEPTH * 0.42f}, {-DESK_WIDTH * 0.42f,  DESK_DEPTH * 0.42f},
        { DESK_WIDTH * 0.42f, -DESK_DEPTH * 0.42f}, {-DESK_WIDTH * 0.42f, -DESK_DEPTH * 0.42f}
    };
    for (int i = 0; i < 4; i++) {
        glm::mat4 legModel = base;
        legModel = glm::translate(legModel, glm::vec3(legOffsets[i][0], DESK_HEIGHT * 0.5f, legOffsets[i][1]));
        legModel = glm::scale(legModel, glm::vec3(0.06f, DESK_HEIGHT, 0.06f));
        renderCube(cubeVAO, shader, legModel, legMaterial);
    }

    Material drawerMaterial = legMaterial;
    glm::mat4 drawerModel = base;
    drawerModel = glm::translate(drawerModel, glm::vec3(-DESK_WIDTH * 0.28f, DESK_HEIGHT * 0.55f, 0.0f));
    drawerModel = glm::scale(drawerModel, glm::vec3(0.32f, DESK_HEIGHT * 0.75f, DESK_DEPTH * 0.85f));
    renderCube(cubeVAO, shader, drawerModel, drawerMaterial);

    // ---- Desk lamp: base sits flush on the desktop's TOP surface
    // (y = DESK_HEIGHT + 0.03, not DESK_HEIGHT — that was the clipping bug) ----
    Material lampMetal;
    lampMetal.ambient = glm::vec3(0.06f, 0.06f, 0.06f);
    lampMetal.diffuse = glm::vec3(0.15f, 0.15f, 0.16f);
    lampMetal.specular = glm::vec3(0.5f, 0.5f, 0.5f);
    lampMetal.shininess = 48.0f;

    float deskTopY = DESK_HEIGHT + 0.03f;
    glm::vec3 lampLocalOffset(DESK_WIDTH * 0.28f, 0.0f, -DESK_DEPTH * 0.2f);

    glm::mat4 lampBaseModel = base;
    lampBaseModel = glm::translate(lampBaseModel, glm::vec3(lampLocalOffset.x, deskTopY + 0.02f, lampLocalOffset.z));
    lampBaseModel = glm::scale(lampBaseModel, glm::vec3(0.16f, 0.04f, 0.16f));
    renderCube(cubeVAO, shader, lampBaseModel, lampMetal);

    glm::mat4 lampArmModel = base;
    lampArmModel = glm::translate(lampArmModel, glm::vec3(lampLocalOffset.x, deskTopY + 0.24f, lampLocalOffset.z));
    lampArmModel = glm::rotate(lampArmModel, glm::radians(20.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    lampArmModel = glm::scale(lampArmModel, glm::vec3(0.04f, 0.42f, 0.04f));
    renderCube(cubeVAO, shader, lampArmModel, lampMetal);

    glm::mat4 shadeModel = base;
    shadeModel = glm::translate(shadeModel, glm::vec3(lampLocalOffset.x, deskTopY + 0.42f, lampLocalOffset.z - 0.12f));
    shadeModel = glm::scale(shadeModel, glm::vec3(0.14f, 0.1f, 0.14f));
    renderCube(cubeVAO, shader, shadeModel, lampMetal);

    // The glowing bulb sits just under the shade — this world position must
    // match ProceduralGenerator::GetDeskLampAnchor() so the visible bulb and
    // the actual spotlight source line up.
    glm::vec3 bulbWorldPos = ProceduralGenerator::GetDeskLampAnchor(room);
    glm::mat4 bulbModel = glm::mat4(1.0f);
    bulbModel = glm::translate(bulbModel, bulbWorldPos);
    lampBulb.drawSphere(shader, bulbModel);
}

// ============================================================================
// SOFA — placed against whichever wall ProceduralGenerator says (Lounge:
// opposite the TV; Fireside/Reading: the fireplace's wall, but set back into
// the room), always facing the right way thanks to the shared wall-local
// coordinate system.
// ============================================================================
void renderSofa(unsigned int& cubeVAO, Shader& shader, const RoomCell& room)
{
    int wallSide = ProceduralGenerator::GetSofaWallSide(room);
    float length = ProceduralGenerator::GetWallLength(wallSide);
    float centerU = length * room.featureUFraction; // never centered, so it can't block a doorway
    float centerW = ProceduralGenerator::GetSofaCenterW(room);
    bool opensAway = ProceduralGenerator::SofaOpensAwayFromWall(room);

    glm::mat4 wallT = ProceduralGenerator::GetWallTransform(room, wallSide);
    auto place = [&](float du, float v, float dw, float su, float sv, float sw) {
        glm::mat4 m = wallT;
        m = glm::translate(m, glm::vec3(centerU + du, v, centerW + dw));
        m = glm::scale(m, glm::vec3(su, sv, sw));
        return m;
        };

    // backrestSign: +1 means the backrest sits at larger w (farther from the
    // wall) — used when the sofa needs to open back toward its own wall.
    float backrestSign = opensAway ? -1.0f : 1.0f;

    Material fabricMaterial;
    fabricMaterial.ambient = room.material.ambient * 0.5f;
    fabricMaterial.diffuse = room.material.diffuse * 0.55f + glm::vec3(0.05f);
    fabricMaterial.specular = glm::vec3(0.08f, 0.08f, 0.08f);
    fabricMaterial.shininess = 6.0f;

    renderCube(cubeVAO, shader, place(0.0f, 0.24f, 0.0f, SOFA_WIDTH, 0.4f, SOFA_DEPTH), fabricMaterial);
    renderCube(cubeVAO, shader, place(0.0f, 0.55f, backrestSign * SOFA_DEPTH * 0.42f, SOFA_WIDTH, 0.6f, 0.16f), fabricMaterial);

    Material armMaterial = fabricMaterial;
    for (float side : { -1.0f, 1.0f }) {
        renderCube(cubeVAO, shader, place(side * (SOFA_WIDTH * 0.5f - 0.12f), 0.42f, 0.0f, 0.22f, 0.34f, SOFA_DEPTH), armMaterial);
    }

    Material legMaterial;
    legMaterial.ambient = glm::vec3(0.14f, 0.09f, 0.06f);
    legMaterial.diffuse = glm::vec3(0.22f, 0.14f, 0.08f);
    legMaterial.specular = glm::vec3(0.1f, 0.1f, 0.1f);
    legMaterial.shininess = 12.0f;

    const float legOffsets[4][2] = {
        { SOFA_WIDTH * 0.42f,  SOFA_DEPTH * 0.4f}, {-SOFA_WIDTH * 0.42f,  SOFA_DEPTH * 0.4f},
        { SOFA_WIDTH * 0.42f, -SOFA_DEPTH * 0.4f}, {-SOFA_WIDTH * 0.42f, -SOFA_DEPTH * 0.4f}
    };
    for (int i = 0; i < 4; i++)
        renderCube(cubeVAO, shader, place(legOffsets[i][0], 0.05f, legOffsets[i][1], 0.06f, 0.1f, 0.06f), legMaterial);

    // Base skirt joining the legs — without this the seat visually "floats"
    // above 4 disconnected little blocks instead of reading as one piece.
    renderCube(cubeVAO, shader, place(0.0f, 0.09f, 0.0f, SOFA_WIDTH - 0.1f, 0.05f, SOFA_DEPTH - 0.1f), legMaterial);
}

// ============================================================================
// TV — flush-mounted panel with a full bezel, wall bracket, and a media
// console beneath it. Placed on whichever wall the Lounge archetype picked.
// ============================================================================
void renderTV(unsigned int& cubeVAO, Shader& shader, const RoomCell& room)
{
    int wallSide = ProceduralGenerator::GetTVWallSide(room);
    float length = ProceduralGenerator::GetWallLength(wallSide);
    float centerU = length * room.featureUFraction; // never centered, so it can't block a doorway

    glm::mat4 wallT = ProceduralGenerator::GetWallTransform(room, wallSide);
    auto place = [&](float du, float v, float w, float su, float sv, float sw) {
        glm::mat4 m = wallT;
        m = glm::translate(m, glm::vec3(centerU + du, v, w));
        m = glm::scale(m, glm::vec3(su, sv, sw));
        return m;
        };

    const float screenH = TV_WIDTH * 0.58f;

    // Bezel (full black border, slightly larger than the screen, sits just
    // behind it so it reads as a frame all the way around)
    Material bezelMaterial;
    bezelMaterial.ambient = glm::vec3(0.03f, 0.03f, 0.03f);
    bezelMaterial.diffuse = glm::vec3(0.06f, 0.06f, 0.06f);
    bezelMaterial.specular = glm::vec3(0.25f, 0.25f, 0.25f);
    bezelMaterial.shininess = 24.0f;
    renderCube(cubeVAO, shader, place(0.0f, 1.35f, 0.08f, TV_WIDTH + 0.08f, screenH + 0.08f, 0.03f), bezelMaterial);

    // Screen (glossy black, in front of the bezel)
    Material screenMaterial;
    screenMaterial.ambient = glm::vec3(0.02f, 0.02f, 0.02f);
    screenMaterial.diffuse = glm::vec3(0.03f, 0.03f, 0.035f);
    screenMaterial.specular = glm::vec3(0.9f, 0.9f, 0.9f);
    screenMaterial.shininess = 96.0f;
    renderCube(cubeVAO, shader, place(0.0f, 1.35f, 0.1f, TV_WIDTH, screenH, 0.03f), screenMaterial);

    // Small power-light accent, bottom-right of the bezel
    Material lightMaterial;
    lightMaterial.ambient = glm::vec3(0.05f, 0.15f, 0.05f);
    lightMaterial.diffuse = glm::vec3(0.1f, 0.9f, 0.2f);
    lightMaterial.specular = glm::vec3(0.2f, 0.9f, 0.2f);
    lightMaterial.shininess = 8.0f;
    renderCube(cubeVAO, shader, place(TV_WIDTH * 0.42f, 1.35f - screenH * 0.48f, 0.115f, 0.02f, 0.02f, 0.01f), lightMaterial);

    // Wall mount bracket connecting the panel back to the wall
    Material mountMaterial;
    mountMaterial.ambient = glm::vec3(0.05f, 0.05f, 0.05f);
    mountMaterial.diffuse = glm::vec3(0.12f, 0.12f, 0.12f);
    mountMaterial.specular = glm::vec3(0.3f, 0.3f, 0.3f);
    mountMaterial.shininess = 20.0f;
    renderCube(cubeVAO, shader, place(0.0f, 1.35f, 0.04f, 0.3f, 0.12f, 0.06f), mountMaterial);

    // Low media console against the same wall, on the floor
    Material consoleMaterial;
    consoleMaterial.ambient = glm::vec3(0.12f, 0.1f, 0.08f);
    consoleMaterial.diffuse = glm::vec3(0.2f, 0.16f, 0.12f);
    consoleMaterial.specular = glm::vec3(0.15f, 0.15f, 0.15f);
    consoleMaterial.shininess = 20.0f;
    float standCenterW = 0.12f + TV_STAND_DEPTH * 0.5f;
    renderCube(cubeVAO, shader, place(0.0f, 0.22f, standCenterW, TV_WIDTH * 0.6f, 0.44f, TV_STAND_DEPTH), consoleMaterial);

    // A raised top shelf and a darker recessed kickboard base — reads as a
    // real cabinet instead of a single bare block
    Material shelfMaterial = consoleMaterial;
    shelfMaterial.diffuse *= 1.1f;
    renderCube(cubeVAO, shader, place(0.0f, 0.445f, standCenterW, TV_WIDTH * 0.66f, 0.03f, TV_STAND_DEPTH + 0.04f), shelfMaterial);

    Material kickboardMaterial;
    kickboardMaterial.ambient = glm::vec3(0.03f, 0.03f, 0.03f);
    kickboardMaterial.diffuse = glm::vec3(0.06f, 0.06f, 0.06f);
    kickboardMaterial.specular = glm::vec3(0.05f, 0.05f, 0.05f);
    kickboardMaterial.shininess = 4.0f;
    renderCube(cubeVAO, shader, place(0.0f, 0.03f, standCenterW, TV_WIDTH * 0.58f, 0.06f, TV_STAND_DEPTH - 0.04f), kickboardMaterial);
}

// ============================================================================
// FIREPLACE — surround flush with the wall, hearth ledge protruding INTO
// the room (previously it protruded backward through the wall).
// ============================================================================
void renderFireplace(unsigned int& cubeVAO, Shader& shader, const RoomCell& room)
{
    int wallSide = ProceduralGenerator::GetFireplaceWallSide(room);
    float length = ProceduralGenerator::GetWallLength(wallSide);
    float centerU = length * room.featureUFraction; // never centered, so it can't block a doorway

    glm::mat4 wallT = ProceduralGenerator::GetWallTransform(room, wallSide);
    auto place = [&](float du, float v, float w, float su, float sv, float sw) {
        glm::mat4 m = wallT;
        m = glm::translate(m, glm::vec3(centerU + du, v, w));
        m = glm::scale(m, glm::vec3(su, sv, sw));
        return m;
        };

    // Stone surround: back flush with the wall (w=0), front face at w=FIREPLACE_DEPTH
    Material stoneMaterial;
    stoneMaterial.ambient = glm::vec3(0.15f, 0.14f, 0.13f);
    stoneMaterial.diffuse = glm::vec3(0.45f, 0.42f, 0.4f);
    stoneMaterial.specular = glm::vec3(0.1f, 0.1f, 0.1f);
    stoneMaterial.shininess = 8.0f;
    renderCube(cubeVAO, shader, place(0.0f, 0.75f, FIREPLACE_DEPTH * 0.5f, FIREPLACE_WIDTH, 1.5f, FIREPLACE_DEPTH), stoneMaterial);

    // Side pillars for a bit more sculptural detail
    Material pillarMaterial = stoneMaterial;
    for (float side : { -1.0f, 1.0f }) {
        renderCube(cubeVAO, shader, place(side * (FIREPLACE_WIDTH * 0.5f + 0.12f), 0.85f, FIREPLACE_DEPTH * 0.55f,
            0.2f, 1.7f, FIREPLACE_DEPTH + 0.1f), pillarMaterial);
    }

    // Mantel ledge on top
    Material mantelMaterial;
    mantelMaterial.ambient = glm::vec3(0.1f, 0.06f, 0.03f);
    mantelMaterial.diffuse = glm::vec3(0.3f, 0.18f, 0.1f);
    mantelMaterial.specular = glm::vec3(0.2f, 0.2f, 0.2f);
    mantelMaterial.shininess = 24.0f;
    renderCube(cubeVAO, shader, place(0.0f, 1.62f, FIREPLACE_DEPTH * 0.5f, FIREPLACE_WIDTH + 0.5f, 0.1f, FIREPLACE_DEPTH + 0.3f), mantelMaterial);

    // Recessed firebox opening
    Material fireboxMaterial;
    fireboxMaterial.ambient = glm::vec3(0.03f, 0.01f, 0.01f);
    fireboxMaterial.diffuse = glm::vec3(0.05f, 0.02f, 0.01f);
    fireboxMaterial.specular = glm::vec3(0.0f, 0.0f, 0.0f);
    fireboxMaterial.shininess = 2.0f;
    renderCube(cubeVAO, shader, place(0.0f, 0.45f, FIREPLACE_DEPTH * 0.75f, FIREPLACE_WIDTH * 0.6f, 0.7f, FIREPLACE_DEPTH * 0.5f), fireboxMaterial);

    // Warm ember-colored back panel, visible inside the firebox opening
    Material emberMaterial;
    emberMaterial.ambient = glm::vec3(0.35f, 0.1f, 0.02f);
    emberMaterial.diffuse = glm::vec3(0.9f, 0.35f, 0.05f);
    emberMaterial.specular = glm::vec3(0.4f, 0.2f, 0.0f);
    emberMaterial.shininess = 16.0f;
    renderCube(cubeVAO, shader, place(0.0f, 0.28f, FIREPLACE_DEPTH * 0.55f, FIREPLACE_WIDTH * 0.5f, 0.18f, FIREPLACE_DEPTH * 0.3f), emberMaterial);

    // Hearth ledge — protrudes further INTO the room, past the surround's
    // front face, instead of back through the wall.
    renderCube(cubeVAO, shader, place(0.0f, 0.06f, FIREPLACE_DEPTH + FIREPLACE_HEARTH_EXTRA * 0.5f,
        FIREPLACE_WIDTH + 0.5f, 0.12f, FIREPLACE_HEARTH_EXTRA), stoneMaterial);
}

// ============================================================================
// CEILING FAN — continuous rotation via glfwGetTime() (Requirement 3)
// ============================================================================
void renderCeilingFan(unsigned int& cubeVAO, Shader& shader, glm::vec3 roomPos, float currentTime, Sphere& lampBulb)
{
    glm::vec3 ceilingCenter = roomPos + glm::vec3(ROOM_WIDTH / 2, ROOM_HEIGHT, ROOM_DEPTH / 2);

    // Brushed-nickel look for the rod/hub/finial
    Material metalMaterial;
    metalMaterial.ambient = glm::vec3(0.14f, 0.14f, 0.15f);
    metalMaterial.diffuse = glm::vec3(0.35f, 0.35f, 0.38f);
    metalMaterial.specular = glm::vec3(0.8f, 0.8f, 0.85f);
    metalMaterial.shininess = 96.0f;

    // Downrod connecting the ceiling to the motor housing
    glm::mat4 rodModel = glm::mat4(1.0f);
    rodModel = glm::translate(rodModel, ceilingCenter - glm::vec3(0.0f, 0.15f, 0.0f));
    rodModel = glm::scale(rodModel, glm::vec3(0.06f, 0.3f, 0.06f));
    renderCube(cubeVAO, shader, rodModel, metalMaterial);

    // Ceiling escutcheon (small flush cap where the rod meets the ceiling)
    glm::mat4 capModel = glm::mat4(1.0f);
    capModel = glm::translate(capModel, ceilingCenter - glm::vec3(0.0f, 0.02f, 0.0f));
    capModel = glm::scale(capModel, glm::vec3(0.22f, 0.04f, 0.22f));
    renderCube(cubeVAO, shader, capModel, metalMaterial);

    glm::vec3 hubPos = ceilingCenter - glm::vec3(0.0f, 0.35f, 0.0f);
    glm::mat4 hubModel = glm::mat4(1.0f);
    hubModel = glm::translate(hubModel, hubPos);
    hubModel = glm::scale(hubModel, glm::vec3(0.32f, 0.16f, 0.32f));
    renderCube(cubeVAO, shader, hubModel, metalMaterial);

    // Small decorative finial capping the bottom of the motor housing
    glm::mat4 finialModel = glm::mat4(1.0f);
    finialModel = glm::translate(finialModel, hubPos - glm::vec3(0.0f, 0.12f, 0.0f));
    finialModel = glm::scale(finialModel, glm::vec3(0.08f, 0.08f, 0.08f));
    renderCube(cubeVAO, shader, finialModel, metalMaterial);

    // Warm-wood blades, tapered (a wider inner segment + a narrower outer
    // tip) instead of one flat rectangular slab, for a less "plank-like"
    // silhouette.
    Material bladeMaterial;
    bladeMaterial.ambient = glm::vec3(0.16f, 0.13f, 0.09f);
    bladeMaterial.diffuse = glm::vec3(0.55f, 0.42f, 0.28f);
    bladeMaterial.specular = glm::vec3(0.25f, 0.22f, 0.18f);
    bladeMaterial.shininess = 20.0f;

    const float innerLen = 0.75f;
    const float outerLen = 0.65f;
    const int bladeCount = 4;
    for (int i = 0; i < bladeCount; i++) {
        float angle = currentTime * 3.2f + glm::radians(i * (360.0f / bladeCount));
        glm::mat4 spin = glm::rotate(glm::translate(glm::mat4(1.0f), hubPos), angle, glm::vec3(0, 1, 0));

        glm::mat4 innerModel = spin;
        innerModel = glm::translate(innerModel, glm::vec3(innerLen * 0.5f, -0.01f, 0.0f));
        innerModel = glm::scale(innerModel, glm::vec3(innerLen, 0.035f, 0.24f));
        renderCube(cubeVAO, shader, innerModel, bladeMaterial);

        glm::mat4 outerModel = spin;
        outerModel = glm::translate(outerModel, glm::vec3(innerLen + outerLen * 0.5f, -0.015f, 0.0f));
        outerModel = glm::scale(outerModel, glm::vec3(outerLen, 0.03f, 0.15f));
        renderCube(cubeVAO, shader, outerModel, bladeMaterial);
    }

    // Light kit: a warm glowing bulb slung under the hub, like a real
    // ceiling-fan/light combo — also makes the fan much easier to spot in
    // a room, since it's no longer just a dim silhouette near the ceiling.
    glm::mat4 glassModel = glm::mat4(1.0f);
    glassModel = glm::translate(glassModel, hubPos - glm::vec3(0.0f, 0.22f, 0.0f));
    lampBulb.drawSphere(shader, glassModel);
}

