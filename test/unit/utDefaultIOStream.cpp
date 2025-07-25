/*-------------------------------------------------------------------------
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
-------------------------------------------------------------------------*/
#include <gtest/gtest.h>
#include "TestIOStream.h"
#include "UnitTestFileGenerator.h"
#include "Tools/TestTools.h"
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace ::Assimp;

class utDefaultIOStream : public ::testing::Test {
    // empty
};

const char data[]{"Lorem ipsum dolor sit amet, consectetur adipiscing elit. Qui\
sque luctus sem diam, ut eleifend arcu auctor eu. Vestibulum id est vel nulla l\
obortis malesuada ut sed turpis. Nulla a volutpat tortor. Nunc vestibulum portt\
itor sapien ornare sagittis volutpat."};

TEST_F( utDefaultIOStream, FileSizeTest ) {
    const auto dataSize = sizeof(data);
    const auto dataCount = dataSize / sizeof(*data);

    char fpath[] = { TMP_PATH"rndfp.XXXXXX\0" };
    std::string tmpName;
    auto *fs = MakeTmpFile(fpath, std::strlen(fpath), tmpName);
    ASSERT_NE(nullptr, fs);
    {
        auto written = std::fwrite(data, sizeof(*data), dataCount, fs );
        EXPECT_NE( 0U, written );

        auto vflush = std::fflush( fs );
        ASSERT_EQ(vflush, 0);

		std::fclose(fs);

        EXPECT_TRUE(Unittest::TestTools::openFilestream(&fs, tmpName.c_str(), "r"));
        ASSERT_NE(nullptr, fs);

        TestDefaultIOStream myStream( fs, fpath);
        size_t size = myStream.FileSize();
        EXPECT_EQ( size, dataSize);
    }
    remove(tmpName.c_str());
}
