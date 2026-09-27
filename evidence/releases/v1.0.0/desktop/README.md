# Desktop ReplayWidget 派生图

PNG 由现有 Host `load_event` 严格解码后，经 PySide6 `ReplayWidget.set_event` 和 `ReplayWidget.export_png` 在 offscreen Qt 中生成。raw 只从外部备份读取，不写入此目录。

`index.json` 列出每张图的 event ID、PNG 哈希和 metadata 文件。逐图 JSON 的 `human_label` 只表示逐事件核实的人工真值（本包均为 null），`planned_action` 是动作计划映射，`ai_prediction` 是 MCU 四分类模型输出；三者始终分开，AI 预测不能反向充当真值。

event 191、195 的 `planned_action=drop` 来自 V1 验收中明确的最后五次掉落计划，但不是逐事件人工真值；event 180、181、202 只保留模型输出和数据完整性字段，没有猜测人工标签。本轮 V1 HIL 动作没有已核实的连续振动人工动作，因此没有为该类制造图片或标签；这不否定受控 AI pilot 已有的连续振动标签。
