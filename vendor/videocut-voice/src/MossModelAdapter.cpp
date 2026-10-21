#include "videocut/ai/model/ModelSource.h"
namespace videocut::ai::model {
const ResolvedModelFile *ResolvedModel::FileForRole(const std::string &role) const noexcept {
    for(const auto &file:files)if(file.role==role)return &file;
    return nullptr;
}
}
