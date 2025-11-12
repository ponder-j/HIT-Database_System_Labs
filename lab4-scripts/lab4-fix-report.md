# Lab 4 Fix Report

This report details the error found during the execution of `python3 query_test_basic.py` and the corresponding fix.

## 1. Error Description

When running the test script `python3 query_test_basic.py`, an assertion failure occurred:

```
rmdb: /mnt/d/rucbase-lab/src/common/common.h:57: void Value::init_raw(int): Assertion `raw == nullptr' failed.
```

This error was triggered during "测试点3: 单表更新与条件查询" (Test Point 3: Single-table update with conditional query), specifically when executing an `UPDATE` statement that affects multiple rows.

## 2. Root Cause Analysis

The assertion `raw == nullptr` is in the `Value::init_raw` function, which is defined in `/mnt/d/rucbase-lab/src/common/common.h`. This function is responsible for allocating memory for a value's raw data representation.

The error occurs within the `UpdateExecutor::Next` method located in `/mnt/d/rucbase-lab/src/execution/executor_update.h`. This method iterates over all record IDs (`rids_`) that need to be updated. Inside this loop, it iterates through the `set_clauses_` to apply the changes.

The problem is that the `set_clauses_` vector is reused for each record update. In the first iteration for the first record, `set_clause.rhs.init_raw()` is called, and the `raw` member of the `Value` object is allocated. When the loop proceeds to the second record, the same `set_clauses_` are used. The `init_raw()` function is called again on the same `Value` object, but its `raw` member is no longer `nullptr`, leading to the assertion failure.

## 3. Solution

To fix this issue, the `raw` member of the `Value` object in each `set_clause` must be reset before calling `init_raw()` again. Since `raw` is a `std::shared_ptr`, we can use its `reset()` method to release the previously allocated memory.

The fix was applied in `/mnt/d/rucbase-lab/src/execution/executor_update.h` by adding a check and a reset call before initializing the raw value.

**Original Code:**
```cpp
// ...
// 初始化右部值的原始数据
set_clause.rhs.init_raw(col->len);
// ...
```

**Fixed Code:**
```cpp
// ...
// 初始化右部值的原始数据
if (set_clause.rhs.raw != nullptr) {
    set_clause.rhs.raw.reset();
}
set_clause.rhs.init_raw(col->len);
// ...
```

After applying this change, I re-ran the `python3 query_test_basic.py` script, and all tests passed successfully, achieving a final score of 100.0. This confirms that the bug has been resolved.
