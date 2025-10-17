bool BufferPoolManager::find_victim_page(frame_id_t* frame_id);

Page* BufferPoolManager::fetch_page(PageId page_id);

bool BufferPoolManager::unpin_page(PageId page_id, bool is_dirty);

bool BufferPoolManager::flush_page(PageId page_id);

Page* BufferPoolManager::new_page(PageId* page_id);

bool BufferPoolManager::delete_page(PageId page_id);

void BufferPoolManager::flush_all_pages(int fd);