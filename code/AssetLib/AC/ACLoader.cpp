/*
---------------------------------------------------------------------------
Open Asset Import Library (assimp)
---------------------------------------------------------------------------

Copyright (c) 2006-2025, assimp team

All rights reserved.

Redistribution and use of this software in source and binary forms,
with or without modification, are permitted provided that the following
conditions are met:

* Redistributions of source code must retain the above
  copyright notice, this list of conditions and the
  following disclaimer.

* Redistributions in binary form must reproduce the above
  copyright notice, this list of conditions and the
  following disclaimer in the documentation and/or other
  materials provided with the distribution.

* Neither the name of the assimp team, nor the names of its
  contributors may be used to endorse or promote products
  derived from this software without specific prior
  written permission of the assimp team.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
---------------------------------------------------------------------------
*/

/** @file Implementation of the AC3D importer class */

#ifndef ASSIMP_BUILD_NO_AC_IMPORTER

// internal headers
#include "ACLoader.h"
#include "Common/Importer.h"
#include <assimp/BaseImporter.h>
#include <assimp/ParsingUtils.h>
#include <assimp/Subdivision.h>
#include <assimp/config.h>
#include <assimp/fast_atof.h>
#include <assimp/importerdesc.h>
#include <assimp/light.h>
#include <assimp/material.h>
#include <assimp/scene.h>
#include <assimp/DefaultLogger.hpp>
#include <assimp/IOSystem.hpp>
#include <assimp/Importer.hpp>
#include <memory>

namespace Assimp {

static constexpr aiImporterDesc desc = {
    "AC3D Importer",
    "",
    "",
    "",
    aiImporterFlags_SupportTextFlavour,
    0,
    0,
    0,
    0,
    "ac acc ac3d"
};

static constexpr auto ACDoubleSidedFlag = 0x20;

// ------------------------------------------------------------------------------------------------
// skip to the next token
inline const char *AcSkipToNextToken(const char *buffer, const char *end) {
    if (!SkipSpaces(&buffer, end)) {
        ASSIMP_LOG_ERROR("AC3D: Unexpected EOF/EOL");
    }
    return buffer;
}

// ------------------------------------------------------------------------------------------------
// read a string (may be enclosed in double quotation marks). buffer must point to "
inline const char *AcGetString(const char *buffer, const char *end, std::string &out) {
    if (*buffer == '\0') {
        throw DeadlyImportError("AC3D: Unexpected EOF in string");
    }
    ++buffer;
    const char *sz = buffer;
    while ('\"' != *buffer && buffer != end) {
        if (IsLineEnd(*buffer)) {
            ASSIMP_LOG_ERROR("AC3D: Unexpected EOF/EOL in string");
            out = "ERROR";
            break;
        }
        ++buffer;
    }
    if (IsLineEnd(*buffer)) {
        return buffer;
    }
    out = std::string(sz, (unsigned int)(buffer - sz));
    ++buffer;

    return buffer;
}

// ------------------------------------------------------------------------------------------------
// read 1 to n floats prefixed with an optional predefined identifier
template <class T>
inline const char *TAcCheckedLoadFloatArray(const char *buffer, const char *end, const char *name, size_t name_length, size_t num, T *out) {
    buffer = AcSkipToNextToken(buffer, end);
    if (0 != name_length) {
        if (0 != strncmp(buffer, name, name_length) || !IsSpace(buffer[name_length])) {
            ASSIMP_LOG_ERROR("AC3D: Unexpected token. ", name, " was expected.");
            return buffer;
        }
        buffer += name_length + 1;
    }
    for (unsigned int _i = 0; _i < num; ++_i) {
        buffer = AcSkipToNextToken(buffer, end);
        buffer = fast_atoreal_move(buffer, ((float *)out)[_i]);
    }

    return buffer;
}

// ------------------------------------------------------------------------------------------------
// Reverses vertex indices in a face.
static void flipWindingOrder(aiFace &f) {
    std::reverse(f.mIndices, f.mIndices + f.mNumIndices);
}

// ------------------------------------------------------------------------------------------------
// Duplicates a face and inverts it. Also duplicates all vertices (so the new face gets its own
// set of normals and isn’t smoothed against the original).
static void buildBacksideOfFace(const aiFace &origFace, aiFace *&outFaces, aiVector3D *&outVertices, const aiVector3D *allVertices,
    aiVector3D *&outUV, const aiVector3D *allUV, unsigned &curIdx) {
    auto &newFace = *outFaces++;
    newFace = origFace;
    flipWindingOrder(newFace);
    for (unsigned f = 0; f < newFace.mNumIndices; ++f) {
        *outVertices++ = allVertices[newFace.mIndices[f]];
        if (outUV) {
            *outUV = allUV[newFace.mIndices[f]];
            outUV++;
        }
        newFace.mIndices[f] = curIdx++;
    }
}

// ------------------------------------------------------------------------------------------------
// Constructor to be privately used by Importer
AC3DImporter::AC3DImporter() :
        mBuffer(),
        configSplitBFCull(),
        configEvalSubdivision(),
        mNumMeshes(),
        mLights(),
        mLightsCounter(0),
        mGroupsCounter(0),
        mPolysCounter(0),
        mWorldsCounter(0) {
    // nothing to be done here
}

// ------------------------------------------------------------------------------------------------
// Returns whether the class can handle the format of the given file.
bool AC3DImporter::CanRead(const std::string &pFile, IOSystem *pIOHandler, bool /*checkSig*/) const {
    static constexpr uint32_t tokens[] = { AI_MAKE_MAGIC("AC3D") };
    return CheckMagicToken(pIOHandler, pFile, tokens, AI_COUNT_OF(tokens));
}

// ------------------------------------------------------------------------------------------------
// Loader meta information
const aiImporterDesc *AC3DImporter::GetInfo() const {
    return &desc;
}

// ------------------------------------------------------------------------------------------------
// Get a pointer to the next line from the file
bool AC3DImporter::GetNextLine() {
    SkipLine(&mBuffer.data, mBuffer.end);
    return SkipSpaces(&mBuffer.data, mBuffer.end);
}

// ------------------------------------------------------------------------------------------------
// Parse an object section in an AC file
bool AC3DImporter::LoadObjectSection(std::vector<Object> &objects) {
    if (!TokenMatch(mBuffer.data, "OBJECT", 6)) {
        return false;
    }

    SkipSpaces(&mBuffer.data, mBuffer.end);

    ++mNumMeshes;

    objects.emplace_back();
    Object &obj = objects.back();

    aiLight *light = nullptr;
    if (!ASSIMP_strincmp(mBuffer.data, "light", 5)) {
        // This is a light source. Add it to the list
        mLights->push_back(light = new aiLight());

        // Return a point light with no attenuation
        light->mType = aiLightSource_POINT;
        light->mColorDiffuse = light->mColorSpecular = aiColor3D(1.f, 1.f, 1.f);
        light->mAttenuationConstant = 1.f;

        // Generate a default name for both the light source and the node
        light->mName.length = ::ai_snprintf(light->mName.data, AI_MAXLEN, "ACLight_%i", static_cast<unsigned int>(mLights->size()) - 1);
        obj.name = std::string(light->mName.data);

        ASSIMP_LOG_VERBOSE_DEBUG("AC3D: Light source encountered");
        obj.type = Object::Light;
    } else if (!ASSIMP_strincmp(mBuffer.data, "group", 5)) {
        obj.type = Object::Group;
    } else if (!ASSIMP_strincmp(mBuffer.data, "world", 5)) {
        obj.type = Object::World;
    } else {
        obj.type = Object::Poly;
    }

    while (GetNextLine()) {
        if (TokenMatch(mBuffer.data, "kids", 4)) {
            SkipSpaces(&mBuffer.data, mBuffer.end);
            unsigned int num = strtoul10(mBuffer.data, &mBuffer.data);
            GetNextLine();
            if (num) {
                // load the children of this object recursively
                obj.children.reserve(num);
                for (unsigned int i = 0; i < num; ++i) {
                    if (!LoadObjectSection(obj.children)) {
                        ASSIMP_LOG_WARN("AC3D: wrong number of kids");
                        break;
                    }
                }
            }
            return true;
        } else if (TokenMatch(mBuffer.data, "name", 4)) {
            SkipSpaces(&mBuffer.data, mBuffer.data);
            mBuffer.data = AcGetString(mBuffer.data, mBuffer.end, obj.name);

            // If this is a light source, we'll also need to store
            // the name of the node in it.
            if (light) {
                light->mName.Set(obj.name);
            }
        } else if (TokenMatch(mBuffer.data, "texture", 7)) {
            SkipSpaces(&mBuffer.data, mBuffer.end);
            std::string texture;
            mBuffer.data = AcGetString(mBuffer.data, mBuffer.end, texture);
            obj.textures.push_back(texture);
        } else if (TokenMatch(mBuffer.data, "texrep", 6)) {
            SkipSpaces(&mBuffer.data, mBuffer.end);
            mBuffer.data = TAcCheckedLoadFloatArray(mBuffer.data, mBuffer.end, "", 0, 2, &obj.texRepeat);
            if (!obj.texRepeat.x || !obj.texRepeat.y)
                obj.texRepeat = aiVector2D(1.f, 1.f);
        } else if (TokenMatch(mBuffer.data, "texoff", 6)) {
            SkipSpaces(&mBuffer.data, mBuffer.end);
            mBuffer.data = TAcCheckedLoadFloatArray(mBuffer.data, mBuffer.end, "", 0, 2, &obj.texOffset);
        } else if (TokenMatch(mBuffer.data, "rot", 3)) {
            SkipSpaces(&mBuffer.data, mBuffer.end);
            mBuffer.data = TAcCheckedLoadFloatArray(mBuffer.data, mBuffer.end, "", 0, 9, &obj.rotation);
        } else if (TokenMatch(mBuffer.data, "loc", 3)) {
            SkipSpaces(&mBuffer.data, mBuffer.end);
            mBuffer.data = TAcCheckedLoadFloatArray(mBuffer.data, mBuffer.end, "", 0, 3, &obj.translation);
        } else if (TokenMatch(mBuffer.data, "subdiv", 6)) {
            SkipSpaces(&mBuffer.data, mBuffer.end);
            obj.subDiv = strtoul10(mBuffer.data, &mBuffer.data);
        } else if (TokenMatch(mBuffer.data, "crease", 6)) {
            SkipSpaces(&mBuffer.data, mBuffer.end);
            obj.crease = fast_atof(mBuffer.data);
        } else if (TokenMatch(mBuffer.data, "numvert", 7)) {
            SkipSpaces(&mBuffer.data, mBuffer.end);

            unsigned int t = strtoul10(mBuffer.data, &mBuffer.data);
            if (t >= AI_MAX_ALLOC(aiVector3D)) {
                throw DeadlyImportError("AC3D: Too many vertices, would run out of memory");
            }
            obj.vertices.reserve(t);
            for (unsigned int i = 0; i < t; ++i) {
                if (!GetNextLine()) {
                    ASSIMP_LOG_ERROR("AC3D: Unexpected EOF: not all vertices have been parsed yet");
                    break;
                } else if (!IsNumeric(*mBuffer.data)) {
                    ASSIMP_LOG_ERROR("AC3D: Unexpected token: not all vertices have been parsed yet");
                    --mBuffer.data; // make sure the line is processed a second time
                    break;
                }
                obj.vertices.emplace_back();
                aiVector3D &v = obj.vertices.back();
                mBuffer.data = TAcCheckedLoadFloatArray(mBuffer.data, mBuffer.end, "", 0, 3, &v.x);
            }
        } else if (TokenMatch(mBuffer.data, "numsurf", 7)) {
            SkipSpaces(&mBuffer.data, mBuffer.end);

            bool Q3DWorkAround = false;

            const unsigned int t = strtoul10(mBuffer.data, &mBuffer.data);
            obj.surfaces.reserve(t);
            for (unsigned int i = 0; i < t; ++i) {
                GetNextLine();
                if (!TokenMatch(mBuffer.data, "SURF", 4)) {
                    // FIX: this can occur for some files - Quick 3D for
                    // example writes no surf chunks
                    if (!Q3DWorkAround) {
                        ASSIMP_LOG_WARN("AC3D: SURF token was expected");
                        ASSIMP_LOG_VERBOSE_DEBUG("Continuing with Quick3D Workaround enabled");
                    }
                    --mBuffer.data; // make sure the line is processed a second time
                    // break; --- see fix notes above

                    Q3DWorkAround = true;
                }
                SkipSpaces(&mBuffer.data, mBuffer.end);
                obj.surfaces.emplace_back();
                Surface &surf = obj.surfaces.back();
                surf.flags = strtoul_cppstyle(mBuffer.data);

                while (true) {
                    if (!GetNextLine()) {
                        throw DeadlyImportError("AC3D: Unexpected EOF: surface is incomplete");
                    }
                    if (TokenMatch(mBuffer.data, "mat", 3)) {
                        SkipSpaces(&mBuffer.data, mBuffer.end);
                        surf.mat = strtoul10(mBuffer.data);
                    } else if (TokenMatch(mBuffer.data, "refs", 4)) {
                        // --- see fix notes above
                        if (Q3DWorkAround) {
                            if (!surf.entries.empty()) {
                                mBuffer.data -= 6;
                                break;
                            }
                        }

                        SkipSpaces(&mBuffer.data, mBuffer.end);
                        const unsigned int m = strtoul10(mBuffer.data);
                        surf.entries.reserve(m);

                        obj.numRefs += m;

                        for (unsigned int k = 0; k < m; ++k) {
                            if (!GetNextLine()) {
                                ASSIMP_LOG_ERROR("AC3D: Unexpected EOF: surface references are incomplete");
                                break;
                            }
                            surf.entries.emplace_back();
                            Surface::SurfaceEntry &entry = surf.entries.back();

                            entry.first = strtoul10(mBuffer.data, &mBuffer.data);
                            SkipSpaces(&mBuffer.data, mBuffer.end);
                            mBuffer.data = TAcCheckedLoadFloatArray(mBuffer.data, mBuffer.end, "", 0, 2, &entry.second);
                        }
                    } else {
                        --mBuffer.data; // make sure the line is processed a second time
                        break;
                    }
                }
            }
        }
    }
    ASSIMP_LOG_ERROR("AC3D: Unexpected EOF: \'kids\' line was expected");

    return false;
}

// ------------------------------------------------------------------------------------------------
// Convert a material from AC3DImporter::Material to aiMaterial
void AC3DImporter::ConvertMaterial(const Object &object,
        const Material &matSrc,
        aiMaterial &matDest) {
    aiString s;

    if (matSrc.name.length()) {
        s.Set(matSrc.name);
        matDest.AddProperty(&s, AI_MATKEY_NAME);
    }
    if (!object.textures.empty()) {
        s.Set(object.textures[0]);
        matDest.AddProperty(&s, AI_MATKEY_TEXTURE_DIFFUSE(0));

        // UV transformation
        if (1.f != object.texRepeat.x || 1.f != object.texRepeat.y ||
                object.texOffset.x || object.texOffset.y) {
            aiUVTransform transform;
            transform.mScaling = object.texRepeat;
            transform.mTranslation = object.texOffset;
            matDest.AddProperty(&transform, 1, AI_MATKEY_UVTRANSFORM_DIFFUSE(0));
        }
    }

    matDest.AddProperty<aiColor3D>(&matSrc.rgb, 1, AI_MATKEY_COLOR_DIFFUSE);
    matDest.AddProperty<aiColor3D>(&matSrc.amb, 1, AI_MATKEY_COLOR_AMBIENT);
    matDest.AddProperty<aiColor3D>(&matSrc.emis, 1, AI_MATKEY_COLOR_EMISSIVE);
    matDest.AddProperty<aiColor3D>(&matSrc.spec, 1, AI_MATKEY_COLOR_SPECULAR);

    int n = -1;
    if (matSrc.shin) {
        n = aiShadingMode_Phong;
        matDest.AddProperty<float>(&matSrc.shin, 1, AI_MATKEY_SHININESS);
    } else {
        n = aiShadingMode_Gouraud;
    }
    matDest.AddProperty<int>(&n, 1, AI_MATKEY_SHADING_MODEL);

    float f = 1.f - matSrc.trans;
    matDest.AddProperty<float>(&f, 1, AI_MATKEY_OPACITY);
}

// ------------------------------------------------------------------------------------------------
// Converts the loaded data to the internal verbose representation
aiNode *AC3DImporter::ConvertObjectSection(Object &object,
        MeshArray &meshes,
        std::vector<aiMaterial *> &outMaterials,
        const std::vector<Material> &materials,
        aiNode *parent) {
    aiNode *node = new aiNode();
    node->mParent = parent;
    if (object.vertices.size()) {
        if (!object.surfaces.size() || !object.numRefs) {
            /* " An object with 7 vertices (no surfaces, no materials defined).
                 This is a good way of getting point data into AC3D.
                 The Vertex->create convex-surface/object can be used on these
                 vertices to 'wrap' a 3d shape around them "
                 (http://www.opencity.info/html/ac3dfileformat.html)

                 therefore: if no surfaces are defined return point data only
             */

            ASSIMP_LOG_INFO("AC3D: No surfaces defined in object definition, "
                            "a point list is returned");

            meshes.push_back(new aiMesh());
            aiMesh *mesh = meshes.back();

            mesh->mNumFaces = mesh->mNumVertices = (unsigned int)object.vertices.size();
            aiFace *faces = mesh->mFaces = new aiFace[mesh->mNumFaces];
            aiVector3D *verts = mesh->mVertices = new aiVector3D[mesh->mNumVertices];

            for (unsigned int i = 0; i < mesh->mNumVertices; ++i, ++faces, ++verts) {
                *verts = object.vertices[i];
                faces->mNumIndices = 1;
                faces->mIndices = new unsigned int[1];
                faces->mIndices[0] = i;
            }

            // use the primary material in this case. this should be the
            // default material if all objects of the file contain points
            // and no faces.
            mesh->mMaterialIndex = 0;
            outMaterials.push_back(new aiMaterial());
            ConvertMaterial(object, materials[0], *outMaterials.back());
        } else {
            // need to generate one or more meshes for this object.
            // find out how many different materials we have
            typedef std::pair<unsigned int, unsigned int> IntPair;
            typedef std::vector<IntPair> MatTable;
            MatTable needMat(materials.size(), IntPair(0, 0));

            std::vector<Surface>::iterator it, end = object.surfaces.end();
            std::vector<Surface::SurfaceEntry>::iterator it2, end2;

            for (it = object.surfaces.begin(); it != end; ++it) {
                unsigned int idx = (*it).mat;
                if (idx >= needMat.size()) {
                    ASSIMP_LOG_ERROR("AC3D: material index is out of range");
                    idx = 0;
                }
                if ((*it).entries.empty()) {
                    ASSIMP_LOG_WARN("AC3D: surface has zero vertex references");
                }
                const bool isDoubleSided = ACDoubleSidedFlag == (it->flags & ACDoubleSidedFlag);
                const int doubleSidedFactor = isDoubleSided ? 2 : 1;

                // validate all vertex indices to make sure we won't crash here
                for (it2 = (*it).entries.begin(),
                    end2 = (*it).entries.end();
                        it2 != end2; ++it2) {
                    if ((*it2).first >= object.vertices.size()) {
                        ASSIMP_LOG_WARN("AC3D: Invalid vertex reference");
                        (*it2).first = 0;
                    }
                }

                if (!needMat[idx].first) {
                    ++node->mNumMeshes;
                }

                switch ((*it).GetType()) {
                case Surface::ClosedLine: // closed line
                    needMat[idx].first += static_cast<unsigned int>((*it).entries.size());
                    needMat[idx].second += static_cast<unsigned int>((*it).entries.size() << 1u);
                    break;

                    // unclosed line
                case Surface::OpenLine:
                    needMat[idx].first += static_cast<unsigned int>((*it).entries.size() - 1);
                    needMat[idx].second += static_cast<unsigned int>(((*it).entries.size() - 1) << 1u);
                    break;

                    // triangle strip
                case Surface::TriangleStrip:
                    needMat[idx].first += static_cast<unsigned int>(it->entries.size() - 2) * doubleSidedFactor;
                    needMat[idx].second += static_cast<unsigned int>(it->entries.size() - 2) * 3 * doubleSidedFactor;
                    break;

                default:
                    // Coerce unknowns to a polygon and warn
                    ASSIMP_LOG_WARN("AC3D: The type flag of a surface is unknown: ", (*it).flags);
                    (*it).flags &= ~(Surface::Mask);
                    // fallthrough

                    // polygon
                case Surface::Polygon:
                    // the number of faces increments by one, the number
                    // of vertices by surface.numref.
                    needMat[idx].first += doubleSidedFactor;
                    needMat[idx].second += static_cast<unsigned int>(it->entries.size()) * doubleSidedFactor;
                };
            }
            unsigned int *pip = node->mMeshes = new unsigned int[node->mNumMeshes];
            unsigned int mat = 0;
            const size_t oldm = meshes.size();
            for (MatTable::const_iterator cit = needMat.begin(), cend = needMat.end();
                    cit != cend; ++cit, ++mat) {
                if (!(*cit).first) {
                    continue;
                }

                // allocate a new aiMesh object
                *pip++ = (unsigned int)meshes.size();
                aiMesh *mesh = new aiMesh();
                meshes.push_back(mesh);

                mesh->mMaterialIndex = static_cast<unsigned int>(outMaterials.size());
                outMaterials.push_back(new aiMaterial());
                ConvertMaterial(object, materials[mat], *outMaterials.back());

                // allocate storage for vertices and normals
                mesh->mNumFaces = (*cit).first;
                if (mesh->mNumFaces == 0) {
                    throw DeadlyImportError("AC3D: No faces");
                } else if (mesh->mNumFaces > AI_MAX_ALLOC(aiFace)) {
                    throw DeadlyImportError("AC3D: Too many faces, would run out of memory");
                }
                aiFace *faces = mesh->mFaces = new aiFace[mesh->mNumFaces];

                mesh->mNumVertices = (*cit).second;
                if (mesh->mNumVertices == 0) {
                    throw DeadlyImportError("AC3D: No vertices");
                } else if (mesh->mNumVertices > AI_MAX_ALLOC(aiVector3D)) {
                    throw DeadlyImportError("AC3D: Too many vertices, would run out of memory");
                }
                aiVector3D *vertices = mesh->mVertices = new aiVector3D[mesh->mNumVertices];
                unsigned int cur = 0;

                // allocate UV coordinates, but only if the texture name for the
                // surface is not empty
                aiVector3D *uv = nullptr;
                if (!object.textures.empty()) {
                    uv = mesh->mTextureCoords[0] = new aiVector3D[mesh->mNumVertices];
                    mesh->mNumUVComponents[0] = 2;
                }

                for (it = object.surfaces.begin(); it != end; ++it) {
                    if (mat == (*it).mat) {
                        const Surface &src = *it;
                        const bool isDoubleSided = ACDoubleSidedFlag == (src.flags & ACDoubleSidedFlag);

                        // closed polygon
                        uint8_t type = (*it).GetType();
                        if (type == Surface::Polygon) {
                            aiFace &face = *faces++;
                            face.mNumIndices = (unsigned int)src.entries.size();
                            if (0 != face.mNumIndices) {
                                face.mIndices = new unsigned int[face.mNumIndices];
                                for (unsigned int i = 0; i < face.mNumIndices; ++i, ++vertices) {
                                    const Surface::SurfaceEntry &entry = src.entries[i];
                                    face.mIndices[i] = cur++;

                                    // copy vertex positions
                                    if (static_cast<unsigned>(vertices - mesh->mVertices) >= mesh->mNumVertices) {
                                        throw DeadlyImportError("AC3D: Invalid number of vertices");
                                    }
                                    *vertices = object.vertices[entry.first] + object.translation;

                                    // copy texture coordinates
                                    if (uv) {
                                        uv->x = entry.second.x;
                                        uv->y = entry.second.y;
                                        ++uv;
                                    }
                                }
                                if(isDoubleSided) // Need a backface?
                                    buildBacksideOfFace(faces[-1], faces, vertices, mesh->mVertices, uv, mesh->mTextureCoords[0], cur);
                            }
                        } else if (type == Surface::TriangleStrip) {
                            for (unsigned int i = 0; i < (unsigned int)src.entries.size() - 2; ++i) {
                                const Surface::SurfaceEntry &entry1 = src.entries[i];
                                const Surface::SurfaceEntry &entry2 = src.entries[i + 1];
                                const Surface::SurfaceEntry &entry3 = src.entries[i + 2];

                                aiFace &face = *faces++;
                                face.mNumIndices = 3;
                                face.mIndices = new unsigned int[face.mNumIndices];
                                face.mIndices[0] = cur++;
                                face.mIndices[1] = cur++;
                                face.mIndices[2] = cur++;
                                if (!(i & 1)) {
                                    *vertices++ = object.vertices[entry1.first] + object.translation;
                                    if (uv) {
                                        uv->x = entry1.second.x;
                                        uv->y = entry1.second.y;
                                        ++uv;
                                    }
                                    *vertices++ = object.vertices[entry2.first] + object.translation;
                                    if (uv) {
                                        uv->x = entry2.second.x;
                                        uv->y = entry2.second.y;
                                        ++uv;
                                    }
                                } else {
                                    *vertices++ = object.vertices[entry2.first] + object.translation;
                                    if (uv) {
                                        uv->x = entry2.second.x;
                                        uv->y = entry2.second.y;
                                        ++uv;
                                    }
                                    *vertices++ = object.vertices[entry1.first] + object.translation;
                                    if (uv) {
                                        uv->x = entry1.second.x;
                                        uv->y = entry1.second.y;
                                        ++uv;
                                    }
                                }
                                if (static_cast<unsigned>(vertices - mesh->mVertices) >= mesh->mNumVertices) {
                                    throw DeadlyImportError("AC3D: Invalid number of vertices");
                                }
                                *vertices++ = object.vertices[entry3.first] + object.translation;
                                if (uv) {
                                    uv->x = entry3.second.x;
                                    uv->y = entry3.second.y;
                                    ++uv;
                                }
                                if(isDoubleSided) // Need a backface?
                                    buildBacksideOfFace(faces[-1], faces, vertices, mesh->mVertices, uv, mesh->mTextureCoords[0], cur);
                            }
                        } else {

                            it2 = (*it).entries.begin();

                            // either a closed or an unclosed line
                            unsigned int tmp = (unsigned int)(*it).entries.size();
                            if (Surface::OpenLine == type) --tmp;
                            for (unsigned int m = 0; m < tmp; ++m) {
                                aiFace &face = *faces++;

                                face.mNumIndices = 2;
                                face.mIndices = new unsigned int[2];
                                face.mIndices[0] = cur++;
                                face.mIndices[1] = cur++;

                                // copy vertex positions
                                if (it2 == (*it).entries.end()) {
                                    throw DeadlyImportError("AC3D: Bad line");
                                }
                                ai_assert((*it2).first < object.vertices.size());
                                *vertices++ = object.vertices[(*it2).first];

                                // copy texture coordinates
                                if (uv) {
                                    uv->x = (*it2).second.x;
                                    uv->y = (*it2).second.y;
                                    ++uv;
                                }

                                if (Surface::ClosedLine == type && tmp - 1 == m) {
                                    // if this is a closed line repeat its beginning now
                                    it2 = (*it).entries.begin();
                                } else
                                    ++it2;

                                // second point
                                *vertices++ = object.vertices[(*it2).first];

                                if (uv) {
                                    uv->x = (*it2).second.x;
                                    uv->y = (*it2).second.y;
                                    ++uv;
                                }
                            }
                        }
                    }
                }
            }

            // Now apply catmull clark subdivision if necessary. We split meshes into
            // materials which is not done by AC3D during smoothing, so we need to
            // collect all meshes using the same material group.
            if (object.subDiv) {
                if (configEvalSubdivision) {
                    std::unique_ptr<Subdivider> div(Subdivider::Create(Subdivider::CATMULL_CLARKE));
                    ASSIMP_LOG_INFO("AC3D: Evaluating subdivision surface: ", object.name);

                    MeshArray cpy(meshes.size() - oldm, nullptr);
                    div->Subdivide(&meshes[oldm], cpy.size(), &cpy.front(), object.subDiv, true);
                    std::copy(cpy.begin(), cpy.end(), meshes.begin() + oldm);

                    // previous meshes are deleted vy Subdivide().
                } else {
                    ASSIMP_LOG_INFO("AC3D: Letting the subdivision surface untouched due to my configuration: ", object.name);
                }
            }
        }
    }

    if (object.name.length())
        node->mName.Set(object.name);
    else {
        // generate a name depending on the type of the node
        switch (object.type) {
        case Object::Group:
            node->mName.length = ::ai_snprintf(node->mName.data, AI_MAXLEN, "ACGroup_%i", mGroupsCounter++);
            break;
        case Object::Poly:
            node->mName.length = ::ai_snprintf(node->mName.data, AI_MAXLEN, "ACPoly_%i", mPolysCounter++);
            break;
        case Object::Light:
            node->mName.length = ::ai_snprintf(node->mName.data, AI_MAXLEN, "ACLight_%i", mLightsCounter++);
            break;

            // there shouldn't be more than one world, but we don't care
        case Object::World:
            node->mName.length = ::ai_snprintf(node->mName.data, AI_MAXLEN, "ACWorld_%i", mWorldsCounter++);
            break;
        }
    }

    // setup the local transformation matrix of the object
    // compute the transformation offset to the parent node
    node->mTransformation = aiMatrix4x4(object.rotation);

    if (object.type == Object::Group || !object.numRefs) {
        node->mTransformation.a4 = object.translation.x;
        node->mTransformation.b4 = object.translation.y;
        node->mTransformation.c4 = object.translation.z;
    }

    // add children to the object
    if (object.children.size()) {
        node->mNumChildren = (unsigned int)object.children.size();
        node->mChildren = new aiNode *[node->mNumChildren];
        for (unsigned int i = 0; i < node->mNumChildren; ++i) {
            node->mChildren[i] = ConvertObjectSection(object.children[i], meshes, outMaterials, materials, node);
        }
    }

    return node;
}

// ------------------------------------------------------------------------------------------------
void AC3DImporter::SetupProperties(const Importer *pImp) {
    configSplitBFCull = pImp->GetPropertyInteger(AI_CONFIG_IMPORT_AC_SEPARATE_BFCULL, 1) ? true : false;
    configEvalSubdivision = pImp->GetPropertyInteger(AI_CONFIG_IMPORT_AC_EVAL_SUBDIVISION, 1) ? true : false;
}

// ------------------------------------------------------------------------------------------------
// Imports the given file into the given scene structure.
void AC3DImporter::InternReadFile(const std::string &pFile,
        aiScene *pScene, IOSystem *pIOHandler) {
    std::unique_ptr<IOStream> file(pIOHandler->Open(pFile, "rb"));

    // Check whether we can read from the file
    if (file == nullptr) {
        throw DeadlyImportError("Failed to open AC3D file ", pFile, ".");
    }

    // allocate storage and copy the contents of the file to a memory buffer
    std::vector<char> mBuffer2;
    TextFileToBuffer(file.get(), mBuffer2);

    mBuffer.data = &mBuffer2[0];
    mBuffer.end = &mBuffer2[0] + mBuffer2.size();
    mNumMeshes = 0;

    mLightsCounter = mPolysCounter = mWorldsCounter = mGroupsCounter = 0;

    if (::strncmp(mBuffer.data, "AC3D", 4)) {
        throw DeadlyImportError("AC3D: No valid AC3D file, magic sequence not found");
    }

    // print the file format version to the console
    unsigned int version = HexDigitToDecimal(mBuffer.data[4]);
    char msg[3];
    ASSIMP_itoa10(msg, 3, version);
    ASSIMP_LOG_INFO("AC3D file format version: ", msg);

    std::vector<Material> materials;
    materials.reserve(5);

    std::vector<Object> rootObjects;
    rootObjects.reserve(5);

    std::vector<aiLight *> lights;
    mLights = &lights;

    while (GetNextLine()) {
        if (TokenMatch(mBuffer.data, "MATERIAL", 8)) {
            materials.emplace_back();
            Material &mat = materials.back();

            // manually parse the material ... sscanf would use the buldin atof ...
            // Format: (name) rgb %f %f %f  amb %f %f %f  emis %f %f %f  spec %f %f %f  shi %d  trans %f

            mBuffer.data = AcSkipToNextToken(mBuffer.data, mBuffer.end);
            if ('\"' == *mBuffer.data) {
                mBuffer.data = AcGetString(mBuffer.data, mBuffer.end, mat.name);
                mBuffer.data = AcSkipToNextToken(mBuffer.data, mBuffer.end);
            }

            mBuffer.data = TAcCheckedLoadFloatArray(mBuffer.data, mBuffer.end, "rgb", 3, 3, &mat.rgb);
            mBuffer.data = TAcCheckedLoadFloatArray(mBuffer.data, mBuffer.end, "amb", 3, 3, &mat.amb);
            mBuffer.data = TAcCheckedLoadFloatArray(mBuffer.data, mBuffer.end, "emis", 4, 3, &mat.emis);
            mBuffer.data = TAcCheckedLoadFloatArray(mBuffer.data, mBuffer.end, "spec", 4, 3, &mat.spec);
            mBuffer.data = TAcCheckedLoadFloatArray(mBuffer.data, mBuffer.end, "shi", 3, 1, &mat.shin);
            mBuffer.data = TAcCheckedLoadFloatArray(mBuffer.data, mBuffer.end, "trans", 5, 1, &mat.trans);
        } else {
            LoadObjectSection(rootObjects);
        }
    }

    if (rootObjects.empty() || mNumMeshes == 0u) {
        throw DeadlyImportError("AC3D: No meshes have been loaded");
    }
    if (materials.empty()) {
        ASSIMP_LOG_WARN("AC3D: No material has been found");
        materials.emplace_back();
    }

    mNumMeshes += (mNumMeshes >> 2u) + 1;
    MeshArray meshes;
    meshes.reserve(mNumMeshes);

    std::vector<aiMaterial *> omaterials;
    materials.reserve(mNumMeshes);

    // generate a dummy root if there are multiple objects on the top layer
    Object *root = nullptr;
    if (1 == rootObjects.size())
        root = &rootObjects[0];
    else {
        root = new Object();
    }

    // now convert the imported stuff to our output data structure
    pScene->mRootNode = ConvertObjectSection(*root, meshes, omaterials, materials);
    if (1 != rootObjects.size()) {
        delete root;
    }

    if (::strncmp(pScene->mRootNode->mName.data, "Node", 4) == 0) {
        pScene->mRootNode->mName.Set("<AC3DWorld>");
    }

    // copy meshes
    if (meshes.empty()) {
        throw DeadlyImportError("An unknown error occurred during converting");
    }
    pScene->mNumMeshes = (unsigned int)meshes.size();
    pScene->mMeshes = new aiMesh *[pScene->mNumMeshes];
    ::memcpy(pScene->mMeshes, &meshes[0], pScene->mNumMeshes * sizeof(void *));

    // copy materials
    pScene->mNumMaterials = (unsigned int)omaterials.size();
    pScene->mMaterials = new aiMaterial *[pScene->mNumMaterials];
    ::memcpy(pScene->mMaterials, &omaterials[0], pScene->mNumMaterials * sizeof(void *));

    // copy lights
    pScene->mNumLights = (unsigned int)lights.size();
    if (!lights.empty()) {
        pScene->mLights = new aiLight *[lights.size()];
        ::memcpy(pScene->mLights, &lights[0], lights.size() * sizeof(void *));
    }
}

} // namespace Assimp

#endif //!defined ASSIMP_BUILD_NO_AC_IMPORTER
