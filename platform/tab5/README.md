# platform/tab5

Tab5 平台层草稿。M2 才接到 HeroesOfJinYong；M1 bring-up **不链接**这些头文件，避免把未实现的 API 编进硬件测试。

core 只应依赖这里的 C 类型，不准 include ESP-IDF 驱动头。
