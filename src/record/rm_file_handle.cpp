/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "rm_file_handle.h"

/**
 * @description: 获取当前表中记录号为rid的记录
 * @param {Rid&} rid 记录号，指定记录的位置
 * @param {Context*} context
 * @return {unique_ptr<RmRecord>} rid对应的记录对象指针
 */
std::unique_ptr<RmRecord> RmFileHandle::get_record(const Rid& rid, Context* context) const {
    // Todo:
    // 1. 获取指定记录所在的page handle
    // 2. 初始化一个指向RmRecord的指针（赋值其内部的data和size）

    // Implementation:
    // 1. 获取指定记录所在的page handle
    RmPageHandle page_handle = fetch_page_handle(rid.page_no);

    // 2. 检查该位置是否有记录
    if (!Bitmap::is_set(page_handle.bitmap, rid.slot_no)) {
        return nullptr;
    }

    // 3. 创建RmRecord并返回
    char* slot_data = page_handle.get_slot(rid.slot_no);
    return std::make_unique<RmRecord>(file_hdr_.record_size, slot_data);
}

/**
 * @description: 在当前表中插入一条记录，不指定插入位置
 * @param {char*} buf 要插入的记录的数据
 * @param {Context*} context
 * @return {Rid} 插入的记录的记录号（位置）
 */
Rid RmFileHandle::insert_record(char* buf, Context* context) {
    // Todo:
    // 1. 获取当前未满的page handle
    // 2. 在page handle中找到空闲slot位置
    // 3. 将buf复制到空闲slot位置
    // 4. 更新page_handle.page_hdr中的数据结构
    // 注意考虑插入一条记录后页面已满的情况，需要更新file_hdr_.first_free_page_no

    // Implementation:
    // 1. 获取当前未满的page handle
    RmPageHandle page_handle = create_page_handle();

    // 2. 在page handle中找到空闲slot位置
    int slot_no = Bitmap::first_bit(false, page_handle.bitmap, file_hdr_.num_records_per_page);

    // 3. 将buf复制到空闲slot位置
    char* slot_data = page_handle.get_slot(slot_no);
    memcpy(slot_data, buf, file_hdr_.record_size);

    // 4. 更新bitmap，将该位置标记为已使用
    Bitmap::set(page_handle.bitmap, slot_no);

    // 5. 更新page_hdr中的记录数
    page_handle.page_hdr->num_records++;

    // 6. 检查页面是否已满，如果满了需要更新first_free_page_no
    if (page_handle.page_hdr->num_records == file_hdr_.num_records_per_page) {
        // 页面已满，更新file_hdr的first_free_page_no为下一个空闲页
        file_hdr_.first_free_page_no = page_handle.page_hdr->next_free_page_no;
    }

    // 7. 标记页面为脏页
    buffer_pool_manager_->unpin_page(page_handle.page->get_page_id(), true);

    // 8. 返回插入记录的Rid
    return Rid{page_handle.page->get_page_id().page_no, slot_no};
}

/**
 * @description: 在当前表中的指定位置插入一条记录
 * @param {Rid&} rid 要插入记录的位置
 * @param {char*} buf 要插入记录的数据
 */
void RmFileHandle::insert_record(const Rid& rid, char* buf) {
    
}

/**
 * @description: 删除记录文件中记录号为rid的记录
 * @param {Rid&} rid 要删除的记录的记录号（位置）
 * @param {Context*} context
 */
void RmFileHandle::delete_record(const Rid& rid, Context* context) {
    // Todo:
    // 1. 获取指定记录所在的page handle
    // 2. 更新page_handle.page_hdr中的数据结构
    // 注意考虑删除一条记录后页面未满的情况，需要调用release_page_handle()

    // Implementation:
    // 1. 获取指定记录所在的page handle
    RmPageHandle page_handle = fetch_page_handle(rid.page_no);

    // 2. 检查该位置是否有记录
    if (!Bitmap::is_set(page_handle.bitmap, rid.slot_no)) {
        return; // 该位置没有记录，直接返回
    }

    // 3. 检查删除前页面是否已满
    bool was_full = (page_handle.page_hdr->num_records == file_hdr_.num_records_per_page);

    // 4. 更新bitmap，将该位置标记为空闲
    Bitmap::reset(page_handle.bitmap, rid.slot_no);

    // 5. 更新page_hdr中的记录数
    page_handle.page_hdr->num_records--;

    // 6. 如果删除后页面从满变为未满，需要调用release_page_handle
    if (was_full) {
        release_page_handle(page_handle);
    }

    // 7. 标记页面为脏页
    buffer_pool_manager_->unpin_page(page_handle.page->get_page_id(), true);
}


/**
 * @description: 更新记录文件中记录号为rid的记录
 * @param {Rid&} rid 要更新的记录的记录号（位置）
 * @param {char*} buf 新记录的数据
 * @param {Context*} context
 */
void RmFileHandle::update_record(const Rid& rid, char* buf, Context* context) {
    // Todo:
    // 1. 获取指定记录所在的page handle
    // 2. 更新记录

    // Implementation:
    // 1. 获取指定记录所在的page handle
    RmPageHandle page_handle = fetch_page_handle(rid.page_no);

    // 2. 检查该位置是否有记录
    if (!Bitmap::is_set(page_handle.bitmap, rid.slot_no)) {
        return; // 该位置没有记录，直接返回
    }

    // 3. 获取slot位置并更新数据
    char* slot_data = page_handle.get_slot(rid.slot_no);
    memcpy(slot_data, buf, file_hdr_.record_size);

    // 4. 标记页面为脏页
    buffer_pool_manager_->unpin_page(page_handle.page->get_page_id(), true);
}

/**
 * 以下函数为辅助函数，仅提供参考，可以选择完成如下函数，也可以删除如下函数，在单元测试中不涉及如下函数接口的直接调用
*/
/**
 * @description: 获取指定页面的页面句柄
 * @param {int} page_no 页面号
 * @return {RmPageHandle} 指定页面的句柄
 */
RmPageHandle RmFileHandle::fetch_page_handle(int page_no) const {
    // Todo:
    // 使用缓冲池获取指定页面，并生成page_handle返回给上层
    // if page_no is invalid, throw PageNotExistError exception

    // Implementation:
    // 检查页面号是否有效
    if (page_no >= file_hdr_.num_pages) {
        throw PageNotExistError("", page_no);
    }

    // 使用缓冲池获取指定页面
    Page* page = buffer_pool_manager_->fetch_page(PageId{fd_, page_no});

    // 创建并返回RmPageHandle
    return RmPageHandle(&file_hdr_, page);
}

/**
 * @description: 创建一个新的page handle
 * @return {RmPageHandle} 新的PageHandle
 */
RmPageHandle RmFileHandle::create_new_page_handle() {
    // Todo:
    // 1.使用缓冲池来创建一个新page
    // 2.更新page handle中的相关信息
    // 3.更新file_hdr_

    // Implementation:
    // 1. 使用缓冲池创建一个新页面
    PageId new_page_id = {fd_, file_hdr_.num_pages};
    Page* page = buffer_pool_manager_->new_page(&new_page_id);

    // 2. 创建RmPageHandle
    RmPageHandle page_handle(&file_hdr_, page);

    // 3. 初始化page_hdr
    page_handle.page_hdr->next_free_page_no = RM_NO_PAGE;
    page_handle.page_hdr->num_records = 0;

    // 4. 初始化bitmap
    Bitmap::init(page_handle.bitmap, file_hdr_.bitmap_size);

    // 5. 更新file_hdr_
    file_hdr_.num_pages++;
    file_hdr_.first_free_page_no = page->get_page_id().page_no;

    return page_handle;
}

/**
 * @brief 创建或获取一个空闲的page handle
 *
 * @return RmPageHandle 返回生成的空闲page handle
 * @note pin the page, remember to unpin it outside!
 */
RmPageHandle RmFileHandle::create_page_handle() {
    // Todo:
    // 1. 判断file_hdr_中是否还有空闲页
    //     1.1 没有空闲页：使用缓冲池来创建一个新page；可直接调用create_new_page_handle()
    //     1.2 有空闲页：直接获取第一个空闲页
    // 2. 生成page handle并返回给上层

    // Implementation:
    // 判断是否有空闲页
    if (file_hdr_.first_free_page_no == RM_NO_PAGE) {
        // 没有空闲页，创建新页面
        return create_new_page_handle();
    } else {
        // 有空闲页，获取第一个空闲页
        return fetch_page_handle(file_hdr_.first_free_page_no);
    }
}

/**
 * @description: 当一个页面从没有空闲空间的状态变为有空闲空间状态时，更新文件头和页头中空闲页面相关的元数据
 */
void RmFileHandle::release_page_handle(RmPageHandle&page_handle) {
    // Todo:
    // 当page从已满变成未满，考虑如何更新：
    // 1. page_handle.page_hdr->next_free_page_no
    // 2. file_hdr_.first_free_page_no

    // Implementation:
    // 当页面从已满变成未满时，需要将其加入空闲页面链表
    // 1. 将当前的first_free_page_no设为该页面的next_free_page_no
    page_handle.page_hdr->next_free_page_no = file_hdr_.first_free_page_no;

    // 2. 将该页面设为新的first_free_page_no
    file_hdr_.first_free_page_no = page_handle.page->get_page_id().page_no;
}