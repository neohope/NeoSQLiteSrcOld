# SQLite 1.0.0 内核源码阅读指南

> 本仓库包含早期版本 **SQLite: An SQL Database Built Upon GDBM** 的全部源码。  
> 相比现代 SQLite，这一版体量更小、结构更直观，非常适合作为“数据库内核入门读物”。

本文档主要帮助你：

- 快速了解项目整体结构
- 把握从 SQL 文本到结果输出的主流程
- 找到各个关键模块和核心源码文件

---

## 一、目录结构概览

仓库顶层主要目录和文件：

- `src/`  
  SQLite 库和命令行工具的**核心 C 源码**：
  - SQL 词法/语法解析
  - 查询计划生成（WHERE/SELECT 分析）
  - 虚拟机 VDBE 指令执行
  - GDBM 后端访问等

- `tool/`  
  开发/构建辅助工具：
  - `lemon.c` / `lempar.c`：Lemon LALR(1) 语法解析器生成器及模板
  - `gdbmdump.c`：GDBM 文件内容转储工具

- `test/`  
  回归测试脚本（Tcl）：
  - 一组 `*.test` 文件，覆盖常见 SQL 功能
  - `tester.tcl` 是测试驱动脚本

- `www/`  
  用 Tcl 脚本生成的网页文档（架构图、语言接口说明等），更偏向对外文档。

- 根目录其它文件：
  - `configure` / `Makefile.in`：构建配置脚本
  - `sqlite.h.in`：对外 C API 头文件模板（真正生成的 `sqlite.h` 在构建目录）
  - `README`：原始英文说明（构建方式等）

---

## 二、关键模块与核心文件

### 1. 顶层 API 与库入口（src）

- `main.c`  
  - 对外暴露的 C 接口：`sqlite_open`、`sqlite_exec`、`sqlite_close` 等  
  - 负责：
    - 打开/关闭数据库文件
    - 初始化/延迟初始化数据库 schema（`sqliteInit`）
    - 管理错误信息与 busy 回调

- `sqliteInt.h`  
  - 内部头文件，定义了 SQLite 内部使用的结构体：
    - `sqlite`：数据库句柄
    - `Table` / `Index` / `Column` 等 schema 结构
    - `Vdbe` / `VdbeOp` 等虚拟机结构
    - 解析器、表达式、SELECT 树等内部类型

### 2. 虚拟机执行引擎 VDBE

- `vdbe.c` / `vdbe.h`  
  - VDBE（Virtual DataBase Engine）实现，是执行 SQL 的核心组件。
  - 主要内容：
    - `struct VdbeOp`：虚拟机指令格式（操作码 + P1/P2/P3 操作数）
    - 栈结构、游标结构、Sorter 等运行时对象
    - `sqliteVdbeExec()`：解释执行 VDBE 程序的主循环
    - 辅助接口：`sqliteVdbeCreate`、`sqliteVdbeAddOpList`、`sqliteVdbeTrace` 等

解析器等高层模块不会直接对 GDBM 读写，而是生成 VDBE 指令序列，由 VDBE 统一执行，这是 SQLite 的核心抽象层。

### 3. SQL 解析（词法 + 语法）

- `tokenize.c`  
  - SQL **词法分析器**：
    - 将输入的 SQL 字符串拆分为一个个 token（关键字、标识符、字符串、数字、操作符等）
    - 调用 Lemon 生成的解析器函数，将 token 依次送入语法分析阶段

- `parse.y`（语法描述）  
  - 使用 Lemon 的语法文件，描述 SQL 语法规则以及归约动作调用的 C 函数。  
  - 构建时由 `lemon` 生成 `parse.c`（已经在源码树中给出或在构建时生成）。

- `build.c`  
  - **语法归约动作实现**：当 `parse.y` 中的产生式被归约时调用这里的函数。  
  - 处理的语法类型包括：
    - `CREATE TABLE` / `DROP TABLE`
    - `CREATE INDEX` / `DROP INDEX`
    - 表达式与 ID 列表构造
    - `COPY`、`VACUUM` 等语句
  - 职责：
    - 填充内部 schema 结构（`Table`、`Index` 等）
    - 生成基础的 VDBE 程序骨架

### 4. 表达式与 SELECT / WHERE 处理

- `expr.c`  
  - 表达式树处理：
    - 判断表达式是否为常量（`isConstant`）
    - 处理 `expr IN (SELECT ...)` 形式，为其分配游标
    - 生成在 VDBE 中求值表达式的指令

- `select.c`  
  - `SELECT` 语句处理：
    - 构建 `Select` 语法树结构 (`sqliteSelectNew`)
    - 实现 SELECT 的删除/清理（`sqliteSelectDelete`）
    - 生成处理 SELECT 的 VDBE 代码（包括 DISTINCT、GROUP BY、HAVING、ORDER BY、复合查询等）

- `where.c`  
  - `WHERE` 子句与简单查询规划：
    - 将 WHERE 拆分为若干子表达式（`exprSplit`）
    - 分析哪些条件可以利用索引（`ExprInfo` / `exprAnalyze`）
    - 估算不同表访问方式的代价，决定扫描顺序
    - 生成针对 WHERE 的 VDBE 代码（包括多表连接）

这一组文件共同构成了“从语法树到执行计划”的核心逻辑。

### 5. DML 语句实现（INSERT/DELETE/UPDATE）

- `insert.c`  
  - `INSERT` 语句处理：
    - 支持 `INSERT INTO ... VALUES (...)` 与 `INSERT INTO ... SELECT ...`
    - 处理显式或省略的列名列表
    - 为目标表和所有相关索引生成插入记录的 VDBE 代码

- `delete.c`  
  - `DELETE FROM` 语句处理：
    - 构建只含目标表的 `IdList`
    - 解析并检查 WHERE 表达式
    - 生成扫描、删除表记录及索引条目的 VDBE 指令

- `update.c`  
  - `UPDATE` 语句处理：
    - 确定哪些列被修改以及对应表达式
    - 利用 WHERE 选出需要更新的行
    - 生成更新表记录和索引的 VDBE 指令

这三者一起，构成了 SQLite 在这一版本里对 DML 的基本支持。

### 6. 工具与后端支持

- `util.c`  
  - 项目中广泛使用的工具函数：
    - 内存分配封装（可选调试支持，检测泄漏/越界）
    - 字符串拼接、比较
    - 错误消息构造等

- `dbbe.c` / `dbbe.h`  
  - 数据库后端接口 DBBE 的 GDBM 实现：
    - 管理底层 GDBM 文件的打开、关闭、引用计数
    - 提供键值对读写、遍历等操作
    - 对上层屏蔽 GDBM 细节，使上层逻辑看见的是统一的“表/索引”接口

---

## 三、命令行工具与语言绑定

### 1. 命令行工具 `sqlite`

- `shell.c`  
  - 实现 `sqlite` 可执行程序：
    - 读取标准输入或脚本文件中的 SQL
    - 调用 `sqlite_open` / `sqlite_exec` 访问数据库
    - 打印结果集，支持简单的点命令（如 `.tables` 等）
  - 是验证库行为、观察 VDBE 输出的一个重要入口。

### 2. Tcl 绑定

- `tclsqlite.c`  
  - 向 Tcl 解释器暴露 SQLite 功能：
    - 在 Tcl 脚本中打开数据库、执行 SQL
    - 设置 busy 回调、遍历结果集
  - `test/` 目录里的回归测试脚本就基于这一绑定实现。

---

## 四、从 SQL 到结果的主流程（代码级视角）

下面以 C API 为入口，概述一次典型的查询流程：

1. **打开数据库**
   - 调用：`sqlite_open("file", ...)`（见 `main.c`）  
   - 初始化 `sqlite` 结构，准备后端 DBBE。

2. **执行 SQL**
   - 调用：`sqlite_exec(db, "SELECT ...", callback, ...)`  
   - 内部会：
     - 调用 `sqliteStrRealloc` 等工具函数构造错误消息（`util.c`）
     - 将 SQL 文本传入词法分析器（`tokenize.c`）

3. **词法分析**
   - `tokenize.c` 将输入字符串分解为 token：
     - 关键字（SELECT/WHERE 等）
     - 标识符（表名/列名）
     - 字面量（字符串/数字）等
   - token 被依次送入 Lemon 生成的解析器。

4. **语法分析与语义构造**
   - Lemon 解析器基于 `parse.y` 进行归约。
   - 归约动作调用 `build.c`、`expr.c`、`select.c` 等文件中的函数：
     - 构建表达式树 `Expr`、选择树 `Select`
     - 填充表、索引、列的内部描述结构
     - 在 `Parse` 结构内逐步构建 VDBE 程序骨架

5. **WHERE / SELECT 优化与 VDBE 指令生成**
   - `where.c` 分析 WHERE 子句，选择合适的索引与连接顺序。
   - `select.c` 为 SELECT（含 GROUP BY/ORDER BY/复合查询等）生成完整的 VDBE 指令序列。
   - 对于 INSERT/DELETE/UPDATE，则由 `insert.c` / `delete.c` / `update.c` 生成对应的 VDBE 代码。

6. **执行 VDBE 程序**
   - 当 SQL 语句的 VDBE 程序构建完成后：
     - 调用 `sqliteExec`（`build.c`）执行它：
       - `sqliteVdbeTrace`（可选调试）
       - `sqliteVdbeExec`（在 `vdbe.c` 中）逐条解释执行指令
   - 执行过程中通过 DBBE（`dbbe.c`）访问底层 GDBM 文件。
   - 每输出一行结果，就触发传入的 C 回调或 Tcl 回调。

7. **关闭与清理**
   - 完成后通过 `sqlite_close` 关闭数据库，释放表/索引描述、VDBE 程序、缓存等资源。

---

## 五、推荐的阅读顺序

如果你是以“理解一个简化版数据库内核”为目标，推荐如下顺序阅读代码：

1. **整体入口与 API**
   - `sqlite.h.in`（对外接口声明）
   - `main.c`（库入口、打开/执行/关闭）

2. **执行引擎 VDBE**
   - `vdbe.h`、`vdbe.c`（重点看指令结构、栈与游标、`sqliteVdbeExec` 主循环）

3. **词法与语法**
   - `tokenize.c`（token 流是怎样产生的）
   - `parse.y`（浏览语法规则）
   - 对照 `build.c` 看归约触发的 C 函数。

4. **查询生成与优化**
   - `expr.c`、`select.c`、`where.c`（表达式、SELECT 和 WHERE 的分析与 VDBE 生成）

5. **DML 与存储后端**
   - `insert.c`、`delete.c`、`update.c`（写操作）
   - `dbbe.c`（GDBM 后端接口）

6. **工具、测试与外层工具**
   - `util.c`、`shell.c`、`tclsqlite.c`、`tool/` 与 `test/` 目录。

按这个路径，从“外壳”到“核心”，基本可以把这一版 SQLite 的设计脉络跑通。

