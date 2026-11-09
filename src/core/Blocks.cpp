#include "Blocks.h"

namespace Studio {

const QList<BlockInfo>& blockCatalog() {
    static const QList<BlockInfo> catalog;
    return catalog;
}

const BlockInfo& blockInfo(BlockType) {
    static const BlockInfo info{};
    return info;
}

const QList<ButtonInfo>& buttonCatalog() {
    static const QList<ButtonInfo> catalog;
    return catalog;
}

const QList<ComparisonInfo>& comparisonCatalog() {
    static const QList<ComparisonInfo> catalog;
    return catalog;
}

LogicLoadResult loadLogic(const QString&) {
    return {std::nullopt, "not implemented"};
}

bool saveLogic(const QString&, const LogicProgram&, QString& error) {
    error = "not implemented";
    return false;
}

QList<LogicProblem> validateLogicStructure(const LogicProgram&) {
    return {};
}

QList<LogicProblem> validateLogic(const LogicProgram&, const Scene&) {
    return {};
}

QString newBlockId(const LogicProgram&) {
    return {};
}

QByteArray generateLogicHeader(const LogicProgram&, const Scene&) {
    return {};
}

QByteArray generateEmptyLogicHeader() {
    return {};
}

bool exportLogicHeader(const QString&, const Scene&, QString& error) {
    error = "not implemented";
    return false;
}

}
