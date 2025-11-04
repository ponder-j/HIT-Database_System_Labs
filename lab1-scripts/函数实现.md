# BufferPoolManager实现报告

## BufferPoolManager::find_victim_page

```cpp
bool find_victim_page(frame_id_t* frame_id);
```

**1.方法声明：**

方法名：find_victim_page

返回类型：bool

功能：从free_list或replacer中得到可淘汰帧页的frame_id

参数列表：

|          | **frame_id**           |
| -------- | ---------------------- |
| 类型     | frame_id_t*            |
| 含义     | 帧页id指针，返回成功找到的可替换帧id |

**2.方法实现思路：**

1. 首先检查缓冲池是否有空闲帧（检查`free_list_`是否为空）
2. 如果有空闲帧：从`free_list_`头部获取一个空闲frame_id，并从链表中移除
3. 如果没有空闲帧：调用`replacer_->victim(frame_id)`使用LRU策略选择一个victim页面
4. 返回查找结果（true表示成功，false表示失败）

**3.实现难点：**

需要理解缓冲池管理的两个阶段：空闲帧管理和页面替换。优先使用空闲帧，只有在缓冲池满时才进行页面替换。

---

## BufferPoolManager::fetch_page

```cpp
Page* fetch_page(PageId page_id);
```

**1.方法声明：**

方法名：fetch_page

返回类型：Page*

功能：从buffer pool获取需要的页。如果页表中存在page_id则直接返回并增加pin_count，否则从磁盘读取页面到缓冲池

参数列表：

|          | **page_id**           |
| -------- | --------------------- |
| 类型     | PageId                |
| 含义     | 需要获取的页的PageId  |

**2.方法实现思路：**

1. 使用`std::scoped_lock`加锁保证线程安全
2. 在`page_table_`中查找目标页是否已在缓冲池中
3. 如果页面已在缓冲池：增加`pin_count_`，调用`replacer_->pin(frame_id)`固定页面，返回页面指针
4. 如果页面不在缓冲池：
   - 调用`find_victim_page`获取可用frame
   - 如果victim页面是脏页，先写回磁盘
   - 从磁盘读取目标页到frame
   - 更新页面元数据（id、pin_count、is_dirty）
   - 更新page_table映射关系
   - 固定页面并返回

**3.实现难点：**

需要处理页面置换时的脏页写回，确保数据一致性。同时要正确维护page_table的映射关系和页面的固定状态。

---

## BufferPoolManager::unpin_page

```cpp
bool unpin_page(PageId page_id, bool is_dirty);
```

**1.方法声明：**

方法名：unpin_page

返回类型：bool

功能：取消固定pin_count>0的在缓冲池中的page

参数列表：

|          | **page_id**     | **is_dirty**                    |
| -------- | --------------- | ------------------------------- |
| 类型     | PageId          | bool                            |
| 含义     | 目标page的page_id | 若目标page应该被标记为dirty则为true |

**2.方法实现思路：**

1. 使用`std::scoped_lock`加锁保证线程安全
2. 在`page_table_`中查找目标页，如果不存在则返回false
3. 检查页面的`pin_count_`，如果已经为0则返回false
4. 将`pin_count_`减1
5. 如果减1后`pin_count_`为0，调用`replacer_->unpin(frame_id)`将页面加入可替换列表
6. 根据`is_dirty`参数更新页面的脏标记
7. 返回true

**3.实现难点：**

需要正确理解pin/unpin机制：pin_count为0的页面才能被替换，unpin操作需要与replacer同步。

---

## BufferPoolManager::flush_page

```cpp
bool flush_page(PageId page_id);
```

**1.方法声明：**

方法名：flush_page

返回类型：bool

功能：将目标页写回磁盘，不考虑当前页面是否正在被使用

参数列表：

|          | **page_id**              |
| -------- | ------------------------ |
| 类型     | PageId                   |
| 含义     | 目标页的page_id，不能为INVALID_PAGE_ID |

**2.方法实现思路：**

1. 使用`std::scoped_lock`加锁保证线程安全
2. 在`page_table_`中查找目标页，如果不存在则返回false
3. 调用`disk_manager_->write_page`将页面数据写回磁盘
4. 将页面的`is_dirty_`标记设为false
5. 返回true

**3.实现难点：**

实现相对简单，主要是确保强制将页面写回磁盘，无论页面是否为脏页。

---

## BufferPoolManager::new_page

```cpp
Page* new_page(PageId* page_id);
```

**1.方法声明：**

方法名：new_page

返回类型：Page*

功能：创建一个新的page，即从磁盘中移动一个新建的空page到缓冲池某个位置

参数列表：

|          | **page_id**                       |
| -------- | --------------------------------- |
| 类型     | PageId*                           |
| 含义     | 当成功创建一个新的page时存储其page_id |

**2.方法实现思路：**

1. 使用`std::scoped_lock`加锁保证线程安全
2. 调用`find_victim_page`获得一个可用的frame
3. 如果victim页面是脏页，先写回磁盘
4. 调用`disk_manager_->allocate_page`分配新的page_id
5. 初始化新页面的元数据（id、pin_count=1、is_dirty=false）
6. 清空页面数据（`reset_memory()`）
7. 更新`page_table_`映射关系
8. 调用`replacer_->pin`固定页面
9. 返回新创建的页面指针

**3.实现难点：**

需要协调磁盘空间分配、缓冲池管理和页面初始化，确保新页面正确地被创建和固定。

---

## BufferPoolManager::delete_page

```cpp
bool delete_page(PageId page_id);
```

**1.方法声明：**

方法名：delete_page

返回类型：bool

功能：从buffer_pool删除目标页

参数列表：

|          | **page_id**  |
| -------- | ------------ |
| 类型     | PageId       |
| 含义     | 目标页       |

**2.方法实现思路：**

1. 使用`std::scoped_lock`加锁保证线程安全
2. 在`page_table_`中查找目标页，如果不存在则返回true
3. 检查目标页的`pin_count_`，如果不为0则返回false（页面正在被使用）
4. 如果是脏页，先写回磁盘
5. 从`page_table_`中删除映射关系
6. 重置页面元数据（page_id设为INVALID_PAGE_ID，pin_count=0，is_dirty=false）
7. 清空页面数据
8. 将frame加入`free_list_`以供重用
9. 返回true

**3.实现难点：**

需要确保只有未被固定的页面才能被删除，并正确回收frame资源。

---

## BufferPoolManager::flush_all_pages

```cpp
void flush_all_pages(int fd);
```

**1.方法声明：**

方法名：flush_all_pages

返回类型：void

功能：将buffer_pool中的所有页写回到磁盘

参数列表：

|          | **fd**     |
| -------- | ---------- |
| 类型     | int        |
| 含义     | 文件句柄   |

**2.方法实现思路：**

1. 使用`std::scoped_lock`加锁保证线程安全
2. 遍历缓冲池中的所有页面（`pages_[0]`到`pages_[pool_size_-1]`）
3. 对于每个页面，检查其是否属于指定文件（`page->get_page_id().fd == fd`）且有效（`page_no != INVALID_PAGE_ID`）
4. 如果条件满足，调用`disk_manager_->write_page`将页面写回磁盘
5. 将页面的`is_dirty_`标记设为false

**3.实现难点：**

需要遍历整个缓冲池并正确识别属于指定文件的页面，确保批量写回操作的正确性。