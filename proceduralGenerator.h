#ifndef PROCEDURAL_GENERATOR_H
#define PROCEDURAL_GENERATOR_H

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <vector>
#include <algorithm>

// ============================================================================
// ROOM / FURNITURE DIMENSIONS (single source of truth — used by main.cpp for
// both rendering AND collision so the two can never drift apart)
// ============================================================================
static const float ROOM_WIDTH = 7.0f;
static const float ROOM_HEIGHT = 3.4f;
static const float ROOM_DEPTH = 7.5f;
static const float WALL_THICKNESS = 0.12f;

// Every wall is drawn (and collided against) slightly inset from the exact
// grid boundary. Two neighboring rooms both inset their own wall toward
// their own interior, so the two walls end up a few centimeters apart in
// world space instead of perfectly coplanar — that's what stops the
// z-fighting flicker that used to make shared walls look "unfinished" /
// see-through into the next room.
static const float WALL_RENDER_INSET = 0.03f;

static const float DOOR_WIDTH = 1.3f;
static const float DOOR_HEADER_HEIGHT = 0.7f; // lintel above the door leaf
static const float DOOR_SLIDE_DISTANCE = DOOR_WIDTH * 1.15f;

static const float BED_WIDTH = 1.9f;
static const float BED_DEPTH = 2.3f;

static const float DESK_WIDTH = 1.3f;
static const float DESK_DEPTH = 0.7f;
static const float DESK_HEIGHT = 0.72f;

static const float SOFA_WIDTH = 2.1f;   // along the wall
static const float SOFA_DEPTH = 0.95f;  // wall-to-front axis

static const float FIREPLACE_WIDTH = 1.6f;
static const float FIREPLACE_DEPTH = 0.55f;   // how far the surround itself protrudes
static const float FIREPLACE_HEARTH_EXTRA = 0.4f; // extra ledge beyond the surround

static const float TV_WIDTH = 1.2f;
static const float TV_STAND_DEPTH = 0.4f;

// ----------------------------------------------------------------------------
// Walls, in a room-local sense. Every room has exactly 4, and each can
// independently be a doorway to the neighboring room in that direction.
// ----------------------------------------------------------------------------
enum WallSide {
    WALL_SOUTH = 0, // z = 0            (local -Z boundary)
    WALL_NORTH = 1, // z = ROOM_DEPTH   (local +Z boundary)
    WALL_WEST = 2, // x = 0            (local -X boundary)
    WALL_EAST = 3  // x = ROOM_WIDTH   (local +X boundary)
};

// Floor quadrants, used to place "corner" furniture (bed/desk) with real
// variety instead of always the same fixed spot.
enum RoomQuadrant {
    QUAD_SW = 0, // low x,  low z
    QUAD_SE = 1, // high x, low z
    QUAD_NW = 2, // low x,  high z
    QUAD_NE = 3  // high x, high z
};

enum RoomArchetype {
    ARCHETYPE_STUDY_BEDROOM = 0,  // bed + desk & lamp, each in its own corner
    ARCHETYPE_LOUNGE = 1,         // sofa facing a wall-mounted TV on the opposite wall
    ARCHETYPE_FIRESIDE_SUITE = 2, // bed (a corner) + fireplace (a wall) + sofa facing it
    ARCHETYPE_READING_NOOK = 3    // desk & lamp (a corner) + fireplace (a wall) + sofa facing it
};

struct Material {
    glm::vec3 ambient;
    glm::vec3 diffuse;
    glm::vec3 specular;
    float shininess;
};

// Simple axis-aligned box in the XZ (floor) plane. Height is deliberately
// omitted — every obstacle we generate spans from the floor up past the
// camera's eye height, so a flat 2D test is all collision needs.
struct CollisionBox2D {
    float minX, maxX, minZ, maxZ;
};

struct RoomCell {
    int gridX, gridY, gridZ;
    glm::vec3 position;
    Material material;

    int archetype;
    bool hasBed;
    bool hasDesk;
    bool hasSofa;
    bool hasTV;
    bool hasFireplace;
    bool hasFan;

    // One door state per wall (SOUTH/NORTH/WEST/EAST) — doors can now open
    // in any of the 4 directions, not just front/back.
    bool hasDoor[4];
    float doorOpenAmount[4];

    // Which quadrant the bed / desk sits in (only meaningful when
    // hasBed / hasDesk is true).
    int bedQuadrant;
    int deskQuadrant;
    glm::vec2 bedJitter;
    glm::vec2 deskJitter;

    // Which wall the fireplace (Fireside/Reading) or the TV (Lounge) is
    // mounted on. The sofa's wall is derived from this (see GetSofaWallSide).
    int featureWallSide;
    // Where along that wall (as a fraction of its length) the feature sits.
    // Always 0.25 or 0.75 — deliberately never 0.5 — so it can never be
    // centered on top of a doorway, which is always centered on the wall.
    float featureUFraction;
};

class ProceduralGenerator {
private:
    // ========================================================================
    // DETERMINISTIC 3D HASH FUNCTION (Rule-Based Seed Generation)
    // ========================================================================
    static unsigned int hash3D(int x, int y, int z) {
        unsigned int h = 2166136261u;
        h ^= (x * 73856093u);
        h ^= (y * 19349663u);
        h ^= (z * 83492791u);
        h = (h ^ (h >> 16)) * 2246822519u;
        h = (h ^ (h >> 13)) * 3266489917u;
        h = h ^ (h >> 16);
        return h;
    }

    // Pseudo-random float in [0, 1] from hash
    static float hashToFloat(unsigned int h) {
        return (h & 0xFFFFFF) / 16777215.0f;
    }

    // Basis for a wall's local coordinate system: local (u, v, w) -> world.
    // u runs along the wall, v is height (== world Y, untouched), and w is
    // distance INTO the room from the wall plane (w=0 sits on the wall).
    // The axes are chosen so the mapping never reflects (always det=+1),
    // which matters because reflecting would reverse triangle winding and
    // make cube faces vanish under back-face culling — that was the root
    // cause of furniture/walls looking hollow/"unfinished" from certain
    // sides.
    static void GetWallBasis(int wallSide, glm::vec3& origin, glm::vec3& uAxis, glm::vec3& wAxis) {
        switch (wallSide) {
        case WALL_SOUTH:
            origin = glm::vec3(0.0f, 0.0f, 0.0f);
            uAxis = glm::vec3(1.0f, 0.0f, 0.0f);
            wAxis = glm::vec3(0.0f, 0.0f, 1.0f);
            break;
        case WALL_NORTH:
            origin = glm::vec3(ROOM_WIDTH, 0.0f, ROOM_DEPTH);
            uAxis = glm::vec3(-1.0f, 0.0f, 0.0f);
            wAxis = glm::vec3(0.0f, 0.0f, -1.0f);
            break;
        case WALL_WEST:
            origin = glm::vec3(0.0f, 0.0f, ROOM_DEPTH);
            uAxis = glm::vec3(0.0f, 0.0f, -1.0f);
            wAxis = glm::vec3(1.0f, 0.0f, 0.0f);
            break;
        case WALL_EAST:
        default:
            origin = glm::vec3(ROOM_WIDTH, 0.0f, 0.0f);
            uAxis = glm::vec3(0.0f, 0.0f, 1.0f);
            wAxis = glm::vec3(-1.0f, 0.0f, 0.0f);
            break;
        }
    }

public:
    // ========================================================================
    // RULE 1: THEME/COLOR VARIATION
    // ========================================================================
    static Material GenerateMaterial(int gridX, int gridY, int gridZ) {
        unsigned int seed = hash3D(gridX, gridY, gridZ);
        int theme = (seed >> 0) % 5;

        Material mat;
        mat.shininess = 32.0f;

        switch (theme) {
        case 0: // Warm beige/cream
            mat.diffuse = glm::vec3(0.9f, 0.85f, 0.7f);
            mat.ambient = mat.diffuse * 0.3f;
            mat.specular = glm::vec3(0.2f, 0.2f, 0.2f);
            break;
        case 1: // Cool blue-gray
            mat.diffuse = glm::vec3(0.6f, 0.7f, 0.8f);
            mat.ambient = mat.diffuse * 0.3f;
            mat.specular = glm::vec3(0.3f, 0.3f, 0.35f);
            break;
        case 2: // Elegant dark red
            mat.diffuse = glm::vec3(0.7f, 0.4f, 0.4f);
            mat.ambient = mat.diffuse * 0.3f;
            mat.specular = glm::vec3(0.3f, 0.2f, 0.2f);
            break;
        case 3: // Soft green
            mat.diffuse = glm::vec3(0.6f, 0.75f, 0.6f);
            mat.ambient = mat.diffuse * 0.3f;
            mat.specular = glm::vec3(0.2f, 0.3f, 0.2f);
            break;
        case 4: // Neutral gray
            mat.diffuse = glm::vec3(0.7f, 0.7f, 0.7f);
            mat.ambient = mat.diffuse * 0.3f;
            mat.specular = glm::vec3(0.25f, 0.25f, 0.25f);
            break;
        }
        return mat;
    }

    // ========================================================================
    // DOORS — decided per shared EDGE (not per room), so both rooms on
    // either side of a wall always agree on whether it has a doorway.
    // Canonicalizing on the lower grid coordinate is what guarantees that
    // agreement regardless of which of the two rooms asks first.
    // ========================================================================
    static bool HasDoorOnEdge(int gridX, int gridZ, int wallSide) {
        bool horizontal = (wallSide == WALL_EAST || wallSide == WALL_WEST);
        int neighborX = gridX + (wallSide == WALL_EAST ? 1 : (wallSide == WALL_WEST ? -1 : 0));
        int neighborZ = gridZ + (wallSide == WALL_NORTH ? 1 : (wallSide == WALL_SOUTH ? -1 : 0));

        unsigned int h;
        if (horizontal) {
            int canonicalX = std::min(gridX, neighborX);
            h = hash3D(canonicalX, 777, gridZ);
        }
        else {
            int canonicalZ = std::min(gridZ, neighborZ);
            h = hash3D(gridX, 888, canonicalZ);
        }
        return hashToFloat(h ^ 0xE0E0u) > 0.35f; // ~65% of edges are doorways
    }

    static int OppositeWall(int wallSide) {
        switch (wallSide) {
        case WALL_SOUTH: return WALL_NORTH;
        case WALL_NORTH: return WALL_SOUTH;
        case WALL_WEST:  return WALL_EAST;
        default:         return WALL_WEST;
        }
    }

    // Does wallSide touch the corner that `quadrant` occupies? (Each floor
    // quadrant borders exactly 2 of the 4 walls.)
    static bool WallTouchesQuadrant(int wallSide, int quadrant) {
        switch (quadrant) {
        case QUAD_SW: return wallSide == WALL_SOUTH || wallSide == WALL_WEST;
        case QUAD_SE: return wallSide == WALL_SOUTH || wallSide == WALL_EAST;
        case QUAD_NW: return wallSide == WALL_NORTH || wallSide == WALL_WEST;
        default:      return wallSide == WALL_NORTH || wallSide == WALL_EAST; // QUAD_NE
        }
    }

    // Of the two along-wall placements (0.25 / 0.75), returns whichever sits
    // FARTHER from the given quadrant's corner on this wall. Only meaningful
    // when WallTouchesQuadrant(wallSide, quadrant) is true.
    static float FarFractionFromQuadrant(int wallSide, int quadrant) {
        bool nearIsSmallU;
        switch (wallSide) {
        case WALL_SOUTH: nearIsSmallU = (quadrant == QUAD_SW); break; // u=0 end is the west/SW side
        case WALL_NORTH: nearIsSmallU = (quadrant == QUAD_NE); break; // u=0 end is the east/NE side
        case WALL_WEST:  nearIsSmallU = (quadrant == QUAD_NW); break; // u=0 end is the north/NW side
        default:         nearIsSmallU = (quadrant == QUAD_SE); break; // WALL_EAST: u=0 end is the south/SE side
        }
        return nearIsSmallU ? 0.75f : 0.25f;
    }

    // ========================================================================
    // RULE 2: FURNITURE LAYOUT
    // ========================================================================
    static void DetermineRoomContents(int gridX, int gridY, int gridZ, RoomCell& room) {
        unsigned int seed = hash3D(gridX, gridY, gridZ);

        room.archetype = (seed >> 4) % 4;

        // IMPORTANT: this must be a *freshly hashed*, full-entropy value, not
        // a right-shifted slice of `seed` compared against a fixed 0.5
        // threshold. Shifting first and then XORing with a small constant
        // (the previous version of this line) throws away all but a handful
        // of low-order bits before the comparison, so the result barely
        // ever crosses 0.5 — that's why the ceiling fan effectively never
        // appeared. Hashing gridX/gridZ fresh (like HasDoorOnEdge does)
        // keeps full entropy and gives a genuine ~50% chance.
        unsigned int fanSeed = hash3D(gridX, 999, gridZ);
        room.hasFan = hashToFloat(fanSeed) > 0.5f;

        room.hasBed = (room.archetype == ARCHETYPE_STUDY_BEDROOM || room.archetype == ARCHETYPE_FIRESIDE_SUITE);
        room.hasDesk = (room.archetype == ARCHETYPE_STUDY_BEDROOM || room.archetype == ARCHETYPE_READING_NOOK);
        room.hasFireplace = (room.archetype == ARCHETYPE_FIRESIDE_SUITE || room.archetype == ARCHETYPE_READING_NOOK);
        room.hasSofa = (room.archetype != ARCHETYPE_STUDY_BEDROOM);
        room.hasTV = (room.archetype == ARCHETYPE_LOUNGE);

        for (int i = 0; i < 4; i++) {
            room.hasDoor[i] = HasDoorOnEdge(gridX, gridZ, i);
            room.doorOpenAmount[i] = 0.0f;
        }

        room.bedQuadrant = (seed >> 6) % 4;
        // Ensure the desk lands in a *different* quadrant than the bed so a
        // Study Bedroom never overlaps the two.
        room.deskQuadrant = (room.bedQuadrant + 1 + ((seed >> 9) % 3)) % 4;

        // Wall-mounted furniture (fireplace/TV/sofa) must never sit in front
        // of a doorway, since doors are always centered on their wall. Two
        // layers of defense: prefer a wall that has no door at all here...
        int doorFreeWalls[4];
        int doorFreeCount = 0;
        for (int w = 0; w < 4; w++) {
            if (!room.hasDoor[w]) doorFreeWalls[doorFreeCount++] = w;
        }
        if (doorFreeCount > 0)
            room.featureWallSide = doorFreeWalls[(seed >> 14) % doorFreeCount];
        else
            room.featureWallSide = (seed >> 12) % 4; // rare: all 4 walls have doors

        // ...and regardless of which wall gets picked, place the furniture a
        // quarter of the way along it (never centered) so it can never
        // overlap the doorway gap even if that wall does end up having one.
        room.featureUFraction = ((seed >> 16) & 1u) ? 0.75f : 0.25f;

        // Fireside Suite / Reading Nook also have a corner piece (bed / desk
        // respectively) sharing the room with the fireplace. If the wall we
        // picked for the fireplace happens to be one of the two walls that
        // corner touches, force the fireplace to the FAR end of that wall —
        // otherwise a bed/desk and a fireplace could land right on top of
        // each other in the same corner.
        int companionQuadrant = -1;
        if (room.archetype == ARCHETYPE_FIRESIDE_SUITE) companionQuadrant = room.bedQuadrant;
        else if (room.archetype == ARCHETYPE_READING_NOOK) companionQuadrant = room.deskQuadrant;

        if (companionQuadrant != -1 && WallTouchesQuadrant(room.featureWallSide, companionQuadrant))
            room.featureUFraction = FarFractionFromQuadrant(room.featureWallSide, companionQuadrant);

        auto jitter = [&](unsigned int salt) -> glm::vec2 {
            float jx = (hashToFloat(seed ^ salt) - 0.5f) * 0.5f;
            float jz = (hashToFloat((seed >> 5) ^ salt) - 0.5f) * 0.5f;
            return glm::vec2(jx, jz);
            };
        room.bedJitter = jitter(0x1111u);
        room.deskJitter = jitter(0x2222u);
    }

    // ========================================================================
    // WALL-LOCAL COORDINATE SYSTEM
    // Shared by rendering (doors, fireplace, TV, sofa) and collision so they
    // can never disagree about where an object actually is.
    // ========================================================================
    static glm::mat4 GetWallTransform(const RoomCell& room, int wallSide) {
        glm::vec3 origin, uAxis, wAxis;
        GetWallBasis(wallSide, origin, uAxis, wAxis);
        origin += room.position;

        glm::mat4 m(1.0f);
        m[0] = glm::vec4(uAxis, 0.0f);
        m[1] = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
        m[2] = glm::vec4(wAxis, 0.0f);
        m[3] = glm::vec4(origin, 1.0f);
        return m;
    }

    static glm::vec3 WallLocalToWorld(const RoomCell& room, int wallSide, float u, float w, float v = 0.0f) {
        glm::vec3 origin, uAxis, wAxis;
        GetWallBasis(wallSide, origin, uAxis, wAxis);
        return room.position + origin + u * uAxis + glm::vec3(0.0f, v, 0.0f) + w * wAxis;
    }

    static float GetWallLength(int wallSide) {
        return (wallSide == WALL_SOUTH || wallSide == WALL_NORTH) ? ROOM_WIDTH : ROOM_DEPTH;
    }

    static glm::vec3 GetDoorWorldPosition(const RoomCell& room, int wallSide) {
        return WallLocalToWorld(room, wallSide, GetWallLength(wallSide) * 0.5f, 0.0f);
    }

    // ========================================================================
    // QUADRANT (corner) PLACEMENT — used for the bed and the desk.
    // ========================================================================
    static glm::vec3 GetQuadrantAnchor(const RoomCell& room, int quadrant, float marginX, float marginZ, glm::vec2 jitterVal) {
        float x = (quadrant == QUAD_SW || quadrant == QUAD_NW) ? marginX : (ROOM_WIDTH - marginX);
        float z = (quadrant == QUAD_SW || quadrant == QUAD_SE) ? marginZ : (ROOM_DEPTH - marginZ);
        return room.position + glm::vec3(x + jitterVal.x, 0.0f, z + jitterVal.y);
    }

    // Corner furniture only ever needs a 0/180 flip (it backs up against
    // whichever of the two nearby walls is more natural), which keeps the
    // math simple: a full arbitrary rotation isn't necessary for variety.
    static float GetQuadrantRotationDeg(int quadrant) {
        return (quadrant == QUAD_SW || quadrant == QUAD_SE) ? 180.0f : 0.0f;
    }

    static glm::vec3 RotateLocalOffset(glm::vec3 offset, float angleDeg) {
        float rad = glm::radians(angleDeg);
        float c = cosf(rad), s = sinf(rad);
        return glm::vec3(offset.x * c + offset.z * s, offset.y, -offset.x * s + offset.z * c);
    }

    static glm::vec3 GetBedAnchor(const RoomCell& room) {
        return GetQuadrantAnchor(room, room.bedQuadrant, BED_WIDTH * 0.5f + 0.5f, BED_DEPTH * 0.5f + 0.5f, room.bedJitter);
    }
    static float GetBedRotationDeg(const RoomCell& room) {
        return GetQuadrantRotationDeg(room.bedQuadrant);
    }

    static glm::vec3 GetDeskAnchor(const RoomCell& room) {
        return GetQuadrantAnchor(room, room.deskQuadrant, DESK_WIDTH * 0.5f + 0.6f, DESK_DEPTH * 0.5f + 0.45f, room.deskJitter);
    }
    static float GetDeskRotationDeg(const RoomCell& room) {
        return GetQuadrantRotationDeg(room.deskQuadrant);
    }

    // Nightstand sits just outside the bed's own footprint, offset along
    // the bed's rotated local +X axis.
    static glm::vec3 GetNightstandAnchor(const RoomCell& room) {
        glm::vec3 localOffset(BED_WIDTH * 0.5f + 0.32f, 0.0f, -BED_DEPTH * 0.32f);
        return GetBedAnchor(room) + RotateLocalOffset(localOffset, GetBedRotationDeg(room));
    }

    // Desk lamp sits on the desk's rotated local top surface.
    static glm::vec3 GetDeskLampAnchor(const RoomCell& room) {
        glm::vec3 localOffset(DESK_WIDTH * 0.28f, DESK_HEIGHT + 0.03f, -DESK_DEPTH * 0.2f);
        return GetDeskAnchor(room) + RotateLocalOffset(localOffset, GetDeskRotationDeg(room));
    }

    // ========================================================================
    // WALL-MOUNTED FEATURES — fireplace, TV, and the sofa that faces
    // whichever of the two it shares a room with. All placed centered
    // along their wall (u = length/2); "w" is measured from that wall
    // inward into the room.
    // ========================================================================
    static int GetFireplaceWallSide(const RoomCell& room) { return room.featureWallSide; }
    static int GetTVWallSide(const RoomCell& room) { return room.featureWallSide; }
    static int GetSofaWallSide(const RoomCell& room) {
        return (room.archetype == ARCHETYPE_LOUNGE) ? OppositeWall(room.featureWallSide) : room.featureWallSide;
    }
    // Lounge: sofa's open side faces away from its own wall (toward the TV).
    // Fireside/Reading: sofa's open side faces back toward the fireplace's
    // wall, i.e. toward smaller w instead of larger w.
    static bool SofaOpensAwayFromWall(const RoomCell& room) {
        return room.archetype == ARCHETYPE_LOUNGE;
    }
    static float GetSofaCenterW(const RoomCell& room) {
        return (room.archetype == ARCHETYPE_LOUNGE) ? (0.15f + SOFA_DEPTH * 0.5f) : 2.6f;
    }

    // ========================================================================
    // COLLISION
    // ========================================================================
    static void AddBox(std::vector<CollisionBox2D>& boxes, glm::vec3 center, float width, float depth) {
        CollisionBox2D b;
        b.minX = center.x - width * 0.5f;
        b.maxX = center.x + width * 0.5f;
        b.minZ = center.z - depth * 0.5f;
        b.maxZ = center.z + depth * 0.5f;
        boxes.push_back(b);
    }

    // Builds a world-space AABB from a rectangle expressed in a wall's local
    // (u, w) coordinates. Works no matter which of the 4 walls it's on —
    // no manual width/depth swapping needed for east/west walls.
    static CollisionBox2D WallLocalBox(const RoomCell& room, int wallSide, float centerU, float halfU, float centerW, float halfW) {
        glm::vec3 c1 = WallLocalToWorld(room, wallSide, centerU - halfU, centerW - halfW);
        glm::vec3 c2 = WallLocalToWorld(room, wallSide, centerU + halfU, centerW + halfW);
        CollisionBox2D b;
        b.minX = std::min(c1.x, c2.x); b.maxX = std::max(c1.x, c2.x);
        b.minZ = std::min(c1.z, c2.z); b.maxZ = std::max(c1.z, c2.z);
        return b;
    }

    static std::vector<CollisionBox2D> GetFurnitureCollisionBoxes(const RoomCell& room) {
        std::vector<CollisionBox2D> boxes;

        if (room.hasBed) {
            AddBox(boxes, GetBedAnchor(room), BED_WIDTH, BED_DEPTH);
            glm::vec3 ns = GetNightstandAnchor(room);
            AddBox(boxes, ns, 0.4f, 0.4f);
        }

        if (room.hasDesk)
            AddBox(boxes, GetDeskAnchor(room), DESK_WIDTH, DESK_DEPTH);

        if (room.hasFireplace) {
            int ws = GetFireplaceWallSide(room);
            float centerU = GetWallLength(ws) * room.featureUFraction;
            boxes.push_back(WallLocalBox(room, ws, centerU, FIREPLACE_WIDTH * 0.5f,
                FIREPLACE_DEPTH * 0.5f, FIREPLACE_DEPTH * 0.5f));
        }

        if (room.hasTV) {
            int ws = GetTVWallSide(room);
            float centerU = GetWallLength(ws) * room.featureUFraction;
            float standCenterW = 0.12f + TV_STAND_DEPTH * 0.5f;
            boxes.push_back(WallLocalBox(room, ws, centerU, TV_WIDTH * 0.3f,
                standCenterW, TV_STAND_DEPTH * 0.5f));
        }

        if (room.hasSofa) {
            int ws = GetSofaWallSide(room);
            float centerU = GetWallLength(ws) * room.featureUFraction;
            float centerW = GetSofaCenterW(room);
            boxes.push_back(WallLocalBox(room, ws, centerU, SOFA_WIDTH * 0.5f,
                centerW, SOFA_DEPTH * 0.5f));
        }

        return boxes;
    }

    static std::vector<CollisionBox2D> GetWallCollisionBoxes(const RoomCell& room) {
        std::vector<CollisionBox2D> boxes;
        const float t = WALL_THICKNESS * 0.5f + 0.06f; // small safety margin

        for (int ws = 0; ws < 4; ws++) {
            float length = GetWallLength(ws);
            if (!room.hasDoor[ws]) {
                boxes.push_back(WallLocalBox(room, ws, length * 0.5f, length * 0.5f, WALL_RENDER_INSET, t));
                continue;
            }
            float gapHalf = DOOR_WIDTH / 2.0f;
            float center = length * 0.5f;
            float leftWidth = center - gapHalf;
            float rightWidth = length - (center + gapHalf);

            if (leftWidth > 0.01f)
                boxes.push_back(WallLocalBox(room, ws, leftWidth * 0.5f, leftWidth * 0.5f, WALL_RENDER_INSET, t));
            if (rightWidth > 0.01f)
                boxes.push_back(WallLocalBox(room, ws, length - rightWidth * 0.5f, rightWidth * 0.5f, WALL_RENDER_INSET, t));
        }

        return boxes;
    }

    // The sliding door leaf itself is a moving obstacle: fully solid when
    // closed, and physically out of the way once slid open.
    static CollisionBox2D GetDoorLeafBox(const RoomCell& room, int wallSide) {
        float length = GetWallLength(wallSide);
        float slide = room.doorOpenAmount[wallSide] * DOOR_SLIDE_DISTANCE;
        float centerU = length * 0.5f + slide;
        float t = WALL_THICKNESS * 0.5f + 0.05f;
        return WallLocalBox(room, wallSide, centerU, DOOR_WIDTH * 0.5f, WALL_RENDER_INSET, t);
    }

    // ========================================================================
    // RULE 3: SEAMLESS CONTINUITY — generate a 3x3 grid around the camera
    // ========================================================================
    static std::vector<RoomCell> GenerateRoomGrid(glm::vec3 cameraPos) {
        std::vector<RoomCell> rooms;

        int centerGridX = static_cast<int>(std::floor(cameraPos.x / ROOM_WIDTH));
        int centerGridZ = static_cast<int>(std::floor(cameraPos.z / ROOM_DEPTH));

        for (int dx = -1; dx <= 1; dx++) {
            for (int dz = -1; dz <= 1; dz++) {
                int gridX = centerGridX + dx;
                int gridZ = centerGridZ + dz;

                RoomCell room;
                room.gridX = gridX;
                room.gridY = 0;
                room.gridZ = gridZ;
                room.position = glm::vec3(gridX * ROOM_WIDTH, 0.0f, gridZ * ROOM_DEPTH);
                room.material = GenerateMaterial(gridX, 0, gridZ);
                DetermineRoomContents(gridX, 0, gridZ, room);

                rooms.push_back(room);
            }
        }

        return rooms;
    }

    // Helper: Get room containing position (for door interaction)
    static RoomCell* FindRoomContainingPoint(std::vector<RoomCell>& rooms, glm::vec3 point) {
        for (auto& room : rooms) {
            float minX = room.position.x, maxX = room.position.x + ROOM_WIDTH;
            float minZ = room.position.z, maxZ = room.position.z + ROOM_DEPTH;
            if (point.x >= minX && point.x <= maxX && point.z >= minZ && point.z <= maxZ)
                return &room;
        }
        return nullptr;
    }

    // Helper: Update every door's open/closed state based on camera proximity
    static void UpdateDoors(std::vector<RoomCell>& rooms, glm::vec3 cameraPos) {
        const float DOOR_TRIGGER_DISTANCE = 1.8f;
        const float DOOR_CLOSE_DISTANCE = 3.0f;
        const float DOOR_SPEED = 2.0f; // units per second

        glm::vec3 flatCamera(cameraPos.x, 0.0f, cameraPos.z);

        for (auto& room : rooms) {
            for (int ws = 0; ws < 4; ws++) {
                if (!room.hasDoor[ws]) continue;

                glm::vec3 doorPos = GetDoorWorldPosition(room, ws);
                float dist = glm::distance(flatCamera, glm::vec3(doorPos.x, 0.0f, doorPos.z));

                if (dist < DOOR_TRIGGER_DISTANCE)
                    room.doorOpenAmount[ws] = glm::min(1.0f, room.doorOpenAmount[ws] + 0.016f * DOOR_SPEED);
                else if (dist > DOOR_CLOSE_DISTANCE)
                    room.doorOpenAmount[ws] = glm::max(0.0f, room.doorOpenAmount[ws] - 0.016f * DOOR_SPEED);
            }
        }
    }
};

#endif
