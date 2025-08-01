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

/** @file Implementation of the STL importer class */

#ifndef ASSIMP_BUILD_NO_STL_IMPORTER

#include "STLLoader.h"
#include <assimp/ParsingUtils.h>
#include <assimp/fast_atof.h>
#include <assimp/importerdesc.h>
#include <assimp/scene.h>
#include <assimp/DefaultLogger.hpp>
#include <assimp/IOSystem.hpp>
#include <memory>

namespace Assimp {

namespace {

static constexpr aiImporterDesc desc = {
    "Stereolithography (STL) Importer",
    "",
    "",
    "",
    aiImporterFlags_SupportTextFlavour | aiImporterFlags_SupportBinaryFlavour,
    0,
    0,
    0,
    0,
    "stl"
};

// A valid binary STL buffer should consist of the following elements, in order:
// 1) 80 byte header
// 2) 4 byte face count
// 3) 50 bytes per face
static bool IsBinarySTL(const char *buffer, size_t fileSize) {
    if (fileSize < 84) {
        return false;
    }

    const char *facecount_pos = buffer + 80;
    uint32_t faceCount(0);
    ::memcpy(&faceCount, facecount_pos, sizeof(uint32_t));
    const uint32_t expectedBinaryFileSize = faceCount * 50 + 84;

    return expectedBinaryFileSize == fileSize;
}

static const size_t BufferSize = 500;
static const char UnicodeBoundary = 127;

// An ascii STL buffer will begin with "solid NAME", where NAME is optional.
// Note: The "solid NAME" check is necessary, but not sufficient, to determine
// if the buffer is ASCII; a binary header could also begin with "solid NAME".
static bool IsAsciiSTL(const char *buffer, size_t fileSize) {
    if (IsBinarySTL(buffer, fileSize))
        return false;

    const char *bufferEnd = buffer + fileSize;

    if (!SkipSpaces(&buffer, bufferEnd)) {
        return false;
    }

    if (buffer + 5 >= bufferEnd) {
        return false;
    }

    bool isASCII(strncmp(buffer, "solid", 5) == 0);
    if (isASCII) {
        // A lot of importers are write solid even if the file is binary. So we have to check for ASCII-characters.
        if (fileSize >= BufferSize) {
            isASCII = true;
            for (unsigned int i = 0; i < BufferSize; i++) {
                if (buffer[i] > UnicodeBoundary) {
                    isASCII = false;
                    break;
                }
            }
        }
    }
    return isASCII;
}
} // namespace

// ------------------------------------------------------------------------------------------------
// Constructor to be privately used by Importer
STLImporter::STLImporter() :
        mBuffer(),
        mFileSize(0),
        mScene() {
    // empty
}

// ------------------------------------------------------------------------------------------------
// Destructor, private as well
STLImporter::~STLImporter() = default;

// ------------------------------------------------------------------------------------------------
// Returns whether the class can handle the format of the given file.
bool STLImporter::CanRead(const std::string &pFile, IOSystem *pIOHandler, bool /*checkSig*/) const {
    static const char *tokens[] = { "STL", "solid" };
    return SearchFileHeaderForToken(pIOHandler, pFile, tokens, AI_COUNT_OF(tokens));
}

// ------------------------------------------------------------------------------------------------
const aiImporterDesc *STLImporter::GetInfo() const {
    return &desc;
}

void addFacesToMesh(aiMesh *pMesh) {
    pMesh->mFaces = new aiFace[pMesh->mNumFaces];
    for (unsigned int i = 0, p = 0; i < pMesh->mNumFaces; ++i) {

        aiFace &face = pMesh->mFaces[i];
        face.mIndices = new unsigned int[face.mNumIndices = 3];
        for (unsigned int o = 0; o < 3; ++o, ++p) {
            face.mIndices[o] = p;
        }
    }
}

// ------------------------------------------------------------------------------------------------
// Imports the given file into the given scene structure.
void STLImporter::InternReadFile(const std::string &pFile, aiScene *pScene, IOSystem *pIOHandler) {
    std::unique_ptr<IOStream> file(pIOHandler->Open(pFile, "rb"));

    // Check whether we can read from the file
    if (file == nullptr) {
        throw DeadlyImportError("Failed to open STL file ", pFile, ".");
    }

    mFileSize = file->FileSize();

    // allocate storage and copy the contents of the file to a memory buffer
    // (terminate it with zero)
    std::vector<char> buffer2;
    TextFileToBuffer(file.get(), buffer2);

    mScene = pScene;
    mBuffer = &buffer2[0];

    // the default vertex color is light gray.
    mClrColorDefault.r = mClrColorDefault.g = mClrColorDefault.b = mClrColorDefault.a = 0.6f;

    // allocate a single node
    mScene->mRootNode = new aiNode();

    bool bMatClr = false;

    if (IsBinarySTL(mBuffer, mFileSize)) {
        bMatClr = LoadBinaryFile();
    } else if (IsAsciiSTL(mBuffer, mFileSize)) {
        LoadASCIIFile(mScene->mRootNode);
    } else {
        throw DeadlyImportError("Failed to determine STL storage representation for ", pFile, ".");
    }

    // create a single default material, using a white diffuse color for consistency with
    // other geometric types (e.g., PLY).
    aiMaterial *pcMat = new aiMaterial();
    aiString s;
    s.Set(AI_DEFAULT_MATERIAL_NAME);
    pcMat->AddProperty(&s, AI_MATKEY_NAME);

    aiColor4D clrDiffuse(ai_real(1.0), ai_real(1.0), ai_real(1.0), ai_real(1.0));
    if (bMatClr) {
        clrDiffuse = mClrColorDefault;
    }
    pcMat->AddProperty(&clrDiffuse, 1, AI_MATKEY_COLOR_DIFFUSE);
    pcMat->AddProperty(&clrDiffuse, 1, AI_MATKEY_COLOR_SPECULAR);
    clrDiffuse = aiColor4D(0.05f, 0.05f, 0.05f, 1.0f);
    pcMat->AddProperty(&clrDiffuse, 1, AI_MATKEY_COLOR_AMBIENT);

    mScene->mNumMaterials = 1;
    mScene->mMaterials = new aiMaterial *[1];
    mScene->mMaterials[0] = pcMat;

    mBuffer = nullptr;
}

// ------------------------------------------------------------------------------------------------
// Read an ASCII STL file
void STLImporter::LoadASCIIFile(aiNode *root) {
    MeshArray meshes;
    std::vector<aiNode *> nodes;
    const char *sz = mBuffer;
    const char *bufferEnd = mBuffer + mFileSize;
    std::vector<aiVector3D> positionBuffer;
    std::vector<aiVector3D> normalBuffer;

    // try to guess how many vertices we could have
    // assume we'll need 160 bytes for each face
    size_t sizeEstimate = std::max(1ull, mFileSize / 160ull) * 3ull;
    positionBuffer.reserve(sizeEstimate);
    normalBuffer.reserve(sizeEstimate);

    while (IsAsciiSTL(sz, static_cast<unsigned int>(bufferEnd - sz))) {
        std::vector<unsigned int> meshIndices;
        aiMesh *pMesh = new aiMesh();
        pMesh->mMaterialIndex = 0;
        meshIndices.push_back((unsigned int)meshes.size());
        meshes.push_back(pMesh);
        aiNode *node = new aiNode;
        node->mParent = root;
        nodes.push_back(node);
        SkipSpaces(&sz, bufferEnd);
        ai_assert(!IsLineEnd(sz));

        sz += 5; // skip the "solid"
        SkipSpaces(&sz, bufferEnd);
        const char *szMe = sz;
        while (!IsSpaceOrNewLine(*sz)) {
            sz++;
        }

        size_t temp = (size_t)(sz - szMe);
        // setup the name of the node
        if (temp) {
            if (temp >= AI_MAXLEN) {
                throw DeadlyImportError("STL: Node name too long");
            }
            std::string name(szMe, temp);
            node->mName.Set(name.c_str());
            pMesh->mName.Set(name.c_str());
        } else {
            mScene->mRootNode->mName.Set("<STL_ASCII>");
        }

        unsigned int faceVertexCounter = 3;
        for (;;) {
            // go to the next token
            if (!SkipSpacesAndLineEnd(&sz, bufferEnd)) {
                // seems we're finished although there was no end marker
                ASSIMP_LOG_WARN("STL: unexpected EOF. \'endsolid\' keyword was expected");
                break;
            }
            // facet normal -0.13 -0.13 -0.98
            if (!strncmp(sz, "facet", 5) && IsSpaceOrNewLine(*(sz + 5)) && *(sz + 5) != '\0') {

                if (faceVertexCounter != 3) {
                    ASSIMP_LOG_WARN("STL: A new facet begins but the old is not yet complete");
                }
                faceVertexCounter = 0;

                sz += 6;
                SkipSpaces(&sz, bufferEnd);
                if (strncmp(sz, "normal", 6)) {
                    ASSIMP_LOG_WARN("STL: a facet normal vector was expected but not found");
                } else {
                    if (sz[6] == '\0') {
                        throw DeadlyImportError("STL: unexpected EOF while parsing facet");
                    }
                    aiVector3D vn;
                    sz += 7;
                    SkipSpaces(&sz, bufferEnd);
                    sz = fast_atoreal_move(sz, vn.x);
                    SkipSpaces(&sz, bufferEnd);
                    sz = fast_atoreal_move(sz, vn.y);
                    SkipSpaces(&sz, bufferEnd);
                    sz = fast_atoreal_move(sz, vn.z);
                    normalBuffer.emplace_back(vn);
                    normalBuffer.emplace_back(vn);
                    normalBuffer.emplace_back(vn);
                }
            } else if (!strncmp(sz, "vertex", 6) && IsSpaceOrNewLine(*(sz + 6))) { // vertex 1.50000 1.50000 0.00000
                if (faceVertexCounter >= 3) {
                    ASSIMP_LOG_ERROR("STL: a facet with more than 3 vertices has been found");
                    ++sz;
                } else {
                    if (sz[6] == '\0') {
                        throw DeadlyImportError("STL: unexpected EOF while parsing facet");
                    }
                    sz += 7;
                    SkipSpaces(&sz, bufferEnd);
                    positionBuffer.emplace_back();
                    aiVector3D *vn = &positionBuffer.back();
                    sz = fast_atoreal_move(sz, vn->x);
                    SkipSpaces(&sz, bufferEnd);
                    sz = fast_atoreal_move(sz, vn->y);
                    SkipSpaces(&sz, bufferEnd);
                    sz = fast_atoreal_move(sz, vn->z);
                    faceVertexCounter++;
                }
            } else if (!::strncmp(sz, "endsolid", 8)) {
                do {
                    ++sz;
                } while (!IsLineEnd(*sz));
                SkipSpacesAndLineEnd(&sz, bufferEnd);
                // finished!
                break;
            } else { // else skip the whole identifier
                do {
                    ++sz;
                } while (!IsSpaceOrNewLine(*sz));
            }
        }

        if (positionBuffer.empty()) {
            pMesh->mNumFaces = 0;
            ASSIMP_LOG_WARN("STL: mesh is empty or invalid; no data loaded");
        }
        if (positionBuffer.size() % 3 != 0) {
            pMesh->mNumFaces = 0;
            throw DeadlyImportError("STL: Invalid number of vertices");
        }
        if (normalBuffer.size() != positionBuffer.size()) {
            pMesh->mNumFaces = 0;
            throw DeadlyImportError("Normal buffer size does not match position buffer size");
        }

        // only process position buffer when filled, else exception when accessing with index operator
        // see line 353: only warning is triggered
        // see line 373(now): access to empty position buffer with index operator forced exception
        if (!positionBuffer.empty()) {
            pMesh->mNumFaces = static_cast<unsigned int>(positionBuffer.size() / 3);
            pMesh->mNumVertices = static_cast<unsigned int>(positionBuffer.size());
            pMesh->mVertices = new aiVector3D[pMesh->mNumVertices];
            for (size_t i = 0; i < pMesh->mNumVertices; ++i) {
                pMesh->mVertices[i].x = positionBuffer[i].x;
                pMesh->mVertices[i].y = positionBuffer[i].y;
                pMesh->mVertices[i].z = positionBuffer[i].z;
            }
            positionBuffer.clear();
        }
        // also only process normalBuffer when filled, else exception when accessing with index operator
        if (!normalBuffer.empty()) {
            pMesh->mNormals = new aiVector3D[pMesh->mNumVertices];
            for (size_t i = 0; i < pMesh->mNumVertices; ++i) {
                pMesh->mNormals[i].x = normalBuffer[i].x;
                pMesh->mNormals[i].y = normalBuffer[i].y;
                pMesh->mNormals[i].z = normalBuffer[i].z;
            }
            normalBuffer.clear();
        }

        // now copy faces
        addFacesToMesh(pMesh);

        // assign the meshes to the current node
        pushMeshesToNode(meshIndices, node);
    }

    // now add the loaded meshes
    mScene->mNumMeshes = (unsigned int)meshes.size();
    mScene->mMeshes = new aiMesh *[mScene->mNumMeshes];
    for (size_t i = 0; i < meshes.size(); i++) {
        mScene->mMeshes[i] = meshes[i];
    }

    root->mNumChildren = (unsigned int)nodes.size();
    root->mChildren = new aiNode *[root->mNumChildren];
    for (size_t i = 0; i < nodes.size(); ++i) {
        root->mChildren[i] = nodes[i];
    }
}

// ------------------------------------------------------------------------------------------------
// Read a binary STL file
bool STLImporter::LoadBinaryFile() {
    // allocate one mesh
    mScene->mNumMeshes = 1;
    mScene->mMeshes = new aiMesh *[1];
    aiMesh *pMesh = mScene->mMeshes[0] = new aiMesh();
    pMesh->mMaterialIndex = 0;

    // skip the first 80 bytes
    if (mFileSize < 84) {
        throw DeadlyImportError("STL: file is too small for the header");
    }
    bool bIsMaterialise = false;

    // search for an occurrence of "COLOR=" in the header
    const unsigned char *sz2 = (const unsigned char *)mBuffer;
    const unsigned char *const szEnd = sz2 + 80;
    while (sz2 < szEnd) {

        if ('C' == *sz2++ && 'O' == *sz2++ && 'L' == *sz2++ &&
                'O' == *sz2++ && 'R' == *sz2++ && '=' == *sz2++) {

            // read the default vertex color for facets
            bIsMaterialise = true;
            ASSIMP_LOG_INFO("STL: Taking code path for Materialise files");
            const ai_real invByte = (ai_real)1.0 / (ai_real)255.0;
            mClrColorDefault.r = (*sz2++) * invByte;
            mClrColorDefault.g = (*sz2++) * invByte;
            mClrColorDefault.b = (*sz2++) * invByte;
            mClrColorDefault.a = (*sz2++) * invByte;
            break;
        }
    }
    const unsigned char *sz = (const unsigned char *)mBuffer + 80;

    // now read the number of facets
    mScene->mRootNode->mName.Set("<STL_BINARY>");

    pMesh->mNumFaces = *((uint32_t *)sz);
    sz += 4;

    if (mFileSize < 84ull + pMesh->mNumFaces * 50ull) {
        throw DeadlyImportError("STL: file is too small to hold all facets");
    }

    if (!pMesh->mNumFaces) {
        throw DeadlyImportError("STL: file is empty. There are no facets defined");
    }

    pMesh->mNumVertices = pMesh->mNumFaces * 3;

    aiVector3D *vp = pMesh->mVertices = new aiVector3D[pMesh->mNumVertices];
    aiVector3D *vn = pMesh->mNormals = new aiVector3D[pMesh->mNumVertices];

    aiVector3f *theVec;
    aiVector3f theVec3F;

    for (unsigned int i = 0; i < pMesh->mNumFaces; ++i) {
        // NOTE: Blender sometimes writes empty normals ... this is not
        // our fault ... the RemoveInvalidData helper step should fix that

        // There's one normal for the face in the STL; use it three times
        // for vertex normals
        theVec = (aiVector3f *)sz;
        ::memcpy(&theVec3F, theVec, sizeof(aiVector3f));
        vn->x = theVec3F.x;
        vn->y = theVec3F.y;
        vn->z = theVec3F.z;
        *(vn + 1) = *vn;
        *(vn + 2) = *vn;
        ++theVec;
        vn += 3;

        // vertex 1
        ::memcpy(&theVec3F, theVec, sizeof(aiVector3f));
        vp->x = theVec3F.x;
        vp->y = theVec3F.y;
        vp->z = theVec3F.z;
        ++theVec;
        ++vp;

        // vertex 2
        ::memcpy(&theVec3F, theVec, sizeof(aiVector3f));
        vp->x = theVec3F.x;
        vp->y = theVec3F.y;
        vp->z = theVec3F.z;
        ++theVec;
        ++vp;

        // vertex 3
        ::memcpy(&theVec3F, theVec, sizeof(aiVector3f));
        vp->x = theVec3F.x;
        vp->y = theVec3F.y;
        vp->z = theVec3F.z;
        ++theVec;
        ++vp;

        sz = (const unsigned char *)theVec;

        uint16_t color = *((uint16_t *)sz);
        sz += 2;

        if (color & (1 << 15)) {
            // seems we need to take the color
            if (!pMesh->mColors[0]) {
                pMesh->mColors[0] = new aiColor4D[pMesh->mNumVertices];
                for (unsigned int j = 0; j < pMesh->mNumVertices; ++j) {
                    *pMesh->mColors[0]++ = mClrColorDefault;
                }
                pMesh->mColors[0] -= pMesh->mNumVertices;

                ASSIMP_LOG_INFO("STL: Mesh has vertex colors");
            }
            aiColor4D *clr = &pMesh->mColors[0][i * 3];
            clr->a = 1.0;
            const ai_real invVal((ai_real)1.0 / (ai_real)31.0);
            if (bIsMaterialise) // this is reversed
            {
                clr->r = (color & 0x1fu) * invVal;
                clr->g = ((color & (0x1fu << 5)) >> 5u) * invVal;
                clr->b = ((color & (0x1fu << 10)) >> 10u) * invVal;
            } else {
                clr->b = (color & 0x1fu) * invVal;
                clr->g = ((color & (0x1fu << 5)) >> 5u) * invVal;
                clr->r = ((color & (0x1fu << 10)) >> 10u) * invVal;
            }
            // assign the color to all vertices of the face
            *(clr + 1) = *clr;
            *(clr + 2) = *clr;
        }
    }

    // now copy faces
    addFacesToMesh(pMesh);

    aiNode *root = mScene->mRootNode;

    // allocate one node
    aiNode *node = new aiNode();
    node->mParent = root;

    root->mNumChildren = 1u;
    root->mChildren = new aiNode *[root->mNumChildren];
    root->mChildren[0] = node;

    // add all created meshes to the single node
    node->mNumMeshes = mScene->mNumMeshes;
    node->mMeshes = new unsigned int[mScene->mNumMeshes];
    for (unsigned int i = 0; i < mScene->mNumMeshes; ++i) {
        node->mMeshes[i] = i;
    }

    if (bIsMaterialise && !pMesh->mColors[0]) {
        // use the color as diffuse material color
        return true;
    }
    return false;
}

void STLImporter::pushMeshesToNode(std::vector<unsigned int> &meshIndices, aiNode *node) {
    ai_assert(nullptr != node);
    if (meshIndices.empty()) {
        return;
    }

    node->mNumMeshes = static_cast<unsigned int>(meshIndices.size());
    node->mMeshes = new unsigned int[meshIndices.size()];
    for (size_t i = 0; i < meshIndices.size(); ++i) {
        node->mMeshes[i] = meshIndices[i];
    }
    meshIndices.clear();
}

} // namespace Assimp

#endif // !! ASSIMP_BUILD_NO_STL_IMPORTER
