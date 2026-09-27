# 发布等价性说明

`v1.0.0` 的 peeled commit 是发布包基线 `a69b6c6c71b91e1267780c169760eca284b7523b`。本分支新增的是文档、索引、派生 PNG、Host 生成/校验工具和测试；没有改变 firmware、protocol、AI model logic、Flash layout、私有 raw 或已有历史 evidence。

Release BIN/ELF/MAP 哈希和候选 revision 取自 V1 software gate。包内 PNG 是从外部 QSPI backup 派生的公开图，不是可回刷镜像，也不包含原始事件 bytes。任何重新构建都必须重新记录产物哈希，不能沿用本快照。
