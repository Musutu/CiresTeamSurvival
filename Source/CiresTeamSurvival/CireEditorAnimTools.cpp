#include "CireEditorAnimTools.h"

#include "Animation/BlendSpace.h"

bool UCireEditorAnimTools::ResampleBlendSpace(UBlendSpace* BlendSpace)
{
#if WITH_EDITOR
    if (!BlendSpace) return false;
    BlendSpace->Modify();
    BlendSpace->ValidateSampleData();
    BlendSpace->ResampleData();
    BlendSpace->MarkPackageDirty();
    return BlendSpace->GetBlendSpaceData().Triangles.Num() > 0 || BlendSpace->GetBlendSpaceData().Segments.Num() > 0;
#else
    return false;
#endif
}
