/*
Open Asset Import Library (assimp)
----------------------------------------------------------------------

Copyright (c) 2006-2025, assimp team

All rights reserved.

Redistribution and use of this software in source and binary forms,
with or without modification, are permitted provided that the
following conditions are met:

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

----------------------------------------------------------------------
*/

/** @file GltfExporter.h
* Declares the exporter class to write a scene to a gltf/glb file
*/
#ifndef AI_GLTF2EXPORTER_H_INC
#define AI_GLTF2EXPORTER_H_INC

#if !defined(ASSIMP_BUILD_NO_GLTF_IMPORTER) && !defined(ASSIMP_BUILD_NO_GLTF2_IMPORTER)

#include <assimp/material.h>
#include <assimp/types.h>
#include <assimp/defs.h>

#include <map>
#include <memory>
#include <sstream>
#include <vector>

struct aiScene;
struct aiNode;
struct aiMaterial;

namespace glTFCommon {
template <class T>
class Ref;

}

namespace glTF2 {

class Asset;

struct TexProperty;
struct TextureInfo;
struct NormalTextureInfo;
struct OcclusionTextureInfo;
struct Node;
struct Texture;
struct PbrSpecularGlossiness;
struct MaterialSpecular;
struct MaterialSheen;
struct MaterialClearcoat;
struct MaterialTransmission;
struct MaterialVolume;
struct MaterialIOR;
struct MaterialEmissiveStrength;
struct MaterialAnisotropy;

// Vec/matrix types, as raw float arrays
typedef float(vec2)[2];
typedef float(vec3)[3];
typedef float(vec4)[4];

} // namespace glTF2

namespace Assimp {
class IOSystem;
class IOStream;
class ExportProperties;

// ------------------------------------------------------------------------------------------------
/** Helper class to export a given scene to an glTF file. */
// ------------------------------------------------------------------------------------------------
class glTF2Exporter {
public:
    /// Constructor for a specific scene to export
    glTF2Exporter(const char *filename, IOSystem *pIOSystem, const aiScene *pScene,
            const ExportProperties *pProperties, bool binary);
    ~glTF2Exporter();

protected:
    void WriteBinaryData(IOStream *outfile, std::size_t sceneLength);
    void GetTexSampler(const aiMaterial &mat, glTFCommon::Ref<glTF2::Texture> texture, aiTextureType tt, unsigned int slot);
    void GetMatTexProp(const aiMaterial &mat, unsigned int &prop, const char *propName, aiTextureType tt, unsigned int idx);
    void GetMatTexProp(const aiMaterial &mat, float &prop, const char *propName, aiTextureType tt, unsigned int idx);
    void GetMatTex(const aiMaterial &mat, glTFCommon::Ref<glTF2::Texture> &texture, unsigned int &texCoord, aiTextureType tt, unsigned int slot);
    void GetMatTex(const aiMaterial &mat, glTF2::TextureInfo &prop, aiTextureType tt, unsigned int slot);
    void GetMatTex(const aiMaterial &mat, glTF2::NormalTextureInfo &prop, aiTextureType tt, unsigned int slot);
    void GetMatTex(const aiMaterial &mat, glTF2::OcclusionTextureInfo &prop, aiTextureType tt, unsigned int slot);
    aiReturn GetMatColor(const aiMaterial &mat, glTF2::vec4 &prop, const char *propName, int type, int idx) const;
    aiReturn GetMatColor(const aiMaterial &mat, glTF2::vec3 &prop, const char *propName, int type, int idx) const;
    bool GetMatSpecGloss(const aiMaterial &mat, glTF2::PbrSpecularGlossiness &pbrSG);
    bool GetMatSpecular(const aiMaterial &mat, glTF2::MaterialSpecular &specular);
    bool GetMatSheen(const aiMaterial &mat, glTF2::MaterialSheen &sheen);
    bool GetMatClearcoat(const aiMaterial &mat, glTF2::MaterialClearcoat &clearcoat);
    bool GetMatTransmission(const aiMaterial &mat, glTF2::MaterialTransmission &transmission);
    bool GetMatVolume(const aiMaterial &mat, glTF2::MaterialVolume &volume);
    bool GetMatIOR(const aiMaterial &mat, glTF2::MaterialIOR &ior);
    bool GetMatEmissiveStrength(const aiMaterial &mat, glTF2::MaterialEmissiveStrength &emissiveStrength);
    bool GetMatAnisotropy(const aiMaterial &mat, glTF2::MaterialAnisotropy &anisotropy);
    void ExportMetadata();
    void ExportMaterials();
    void ExportMeshes();
    void MergeMeshes();
    unsigned int ExportNodeHierarchy(const aiNode *n);
    unsigned int ExportNode(const aiNode *node, glTFCommon::Ref<glTF2::Node> &parent);
    void ExportScene();
    void ExportAnimations();

private:
    const char *mFilename;
    IOSystem *mIOSystem;
    const aiScene *mScene;
    const ExportProperties *mProperties;
    std::map<std::string, unsigned int> mTexturesByPath;
    std::shared_ptr<glTF2::Asset> mAsset;
    std::vector<unsigned char> mBodyData;
    ai_real configEpsilon;
};

} // namespace Assimp

#endif // ASSIMP_BUILD_NO_GLTF_IMPORTER

#endif // AI_GLTF2EXPORTER_H_INC
