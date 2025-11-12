/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#pragma once
#include "execution_defs.h"
#include "execution_manager.h"
#include "executor_abstract.h"
#include "index/ix.h"
#include "system/sm.h"

class NestedLoopJoinExecutor : public AbstractExecutor {
   private:
    std::unique_ptr<AbstractExecutor> left_;    // 左儿子节点（需要join的表）
    std::unique_ptr<AbstractExecutor> right_;   // 右儿子节点（需要join的表）
    size_t len_;                                // join后获得的每条记录的长度
    std::vector<ColMeta> cols_;                 // join后获得的记录的字段

    std::vector<Condition> fed_conds_;          // join条件
    bool isend;

   public:
    NestedLoopJoinExecutor(std::unique_ptr<AbstractExecutor> left, std::unique_ptr<AbstractExecutor> right, 
                            std::vector<Condition> conds) {
        left_ = std::move(left);
        right_ = std::move(right);
        len_ = left_->tupleLen() + right_->tupleLen();
        cols_ = left_->cols();
        auto right_cols = right_->cols();
        for (auto &col : right_cols) {
            col.offset += left_->tupleLen();
        }

        cols_.insert(cols_.end(), right_cols.begin(), right_cols.end());
        isend = false;
        fed_conds_ = std::move(conds);

    }

    void beginTuple() override {
        // 定位到左子节点算子的第一条结果元组
        left_->beginTuple();
        
        // 如果左子节点不为空，定位到右子节点算子的第一条结果元组
        if (!left_->is_end()) {
            right_->beginTuple();
            
            // 如果当前左右元组不满足连接条件，继续寻找满足条件的元组对
            while (!left_->is_end()) {
                while (!right_->is_end()) {
                    auto left_rec = left_->Next();
                    auto right_rec = right_->Next();
                    // 检查是否满足连接条件
                    if (eval_conds(left_rec.get(), right_rec.get(), fed_conds_, cols_)) {
                        return;  // 找到满足条件的元组对
                    }
                    right_->nextTuple();
                }
                // 右子节点已扫描完毕，移动到左子节点的下一条记录
                left_->nextTuple();
                if (!left_->is_end()) {
                    right_->beginTuple();  // 重新开始扫描右子节点
                }
            }
        }
        
        isend = left_->is_end();
    }

    void nextTuple() override {
        // 确保迭代器已初始化
        assert(!left_->is_end());
        
        // 移动到右子节点的下一条记录
        right_->nextTuple();
        
        // 如果右子节点未到达末尾，检查当前元组对是否满足连接条件
        while (!right_->is_end()) {
            auto left_rec = left_->Next();
            auto right_rec = right_->Next();
            if (eval_conds(left_rec.get(), right_rec.get(), fed_conds_, cols_)) {
                return;  // 找到满足条件的元组对
            }
            right_->nextTuple();
        }
        
        // 右子节点已扫描完毕，移动到左子节点的下一条记录
        left_->nextTuple();
        
        // 如果左子节点未到达末尾，重新开始扫描右子节点
        while (!left_->is_end()) {
            right_->beginTuple();
            
            while (!right_->is_end()) {
                auto left_rec = left_->Next();
                auto right_rec = right_->Next();
                if (eval_conds(left_rec.get(), right_rec.get(), fed_conds_, cols_)) {
                    return;  // 找到满足条件的元组对
                }
                right_->nextTuple();
            }
            
            left_->nextTuple();
        }
        
        // 左子节点已扫描完毕
        isend = true;
    }

    std::unique_ptr<RmRecord> Next() override {
        // 获取左子节点和右子节点的当前元组
        auto left_rec = left_->Next();
        auto right_rec = right_->Next();
        
        // 创建连接后的结果元组
        auto join_rec = std::make_unique<RmRecord>(len_);
        
        // 复制左子节点元组的数据到结果元组
        memcpy(join_rec->data, left_rec->data, left_->tupleLen());
        
        // 复制右子节点元组的数据到结果元组（追加在左元组数据之后）
        memcpy(join_rec->data + left_->tupleLen(), right_rec->data, right_->tupleLen());
        
        return join_rec;
    }

    Rid &rid() override { return _abstract_rid; }

    bool is_end() const override { 
        // 判断是否已经没有结果元组了
        return isend; 
    }
    
    std::string getType() override { return "NestedLoopJoinExecutor"; }

    size_t tupleLen() const override { return len_; }

    const std::vector<ColMeta> &cols() const override { return cols_; }

    private:
    bool eval_cond(const RmRecord *lhs_rec, const RmRecord *rhs_rec, const Condition &cond, const std::vector<ColMeta> &rec_cols) {
        // 获取连接条件左部表达式的列元数据和值
        auto lhs_col = get_col(rec_cols, cond.lhs_col);
        char *lhs_data = nullptr;
        
        // 判断左部列来自左子节点还是右子节点
        if (lhs_col->offset < left_->tupleLen()) {
            // 左部列来自左子节点
            lhs_data = lhs_rec->data + lhs_col->offset;
        } else {
            // 左部列来自右子节点，需要调整偏移量
            lhs_data = rhs_rec->data + (lhs_col->offset - left_->tupleLen());
        }
        
        char *rhs_data = nullptr;
        ColType rhs_type;
        
        // 判断右部是值还是列
        if (cond.is_rhs_val) {
            // 右部是常量值
            rhs_type = cond.rhs_val.type;
            rhs_data = cond.rhs_val.raw->data;
        } else {
            // 右部是列
            auto rhs_col = get_col(rec_cols, cond.rhs_col);
            rhs_type = rhs_col->type;
            
            // 判断右部列来自左子节点还是右子节点
            if (rhs_col->offset < left_->tupleLen()) {
                // 右部列来自左子节点
                rhs_data = lhs_rec->data + rhs_col->offset;
            } else {
                // 右部列来自右子节点，需要调整偏移量
                rhs_data = rhs_rec->data + (rhs_col->offset - left_->tupleLen());
            }
        }
        
        // 使用ix_compare函数比较左部和右部的值
        int cmp = ix_compare(lhs_data, rhs_data, lhs_col->type, lhs_col->len);
        
        // 根据比较运算符判断条件是否满足
        switch (cond.op) {
            case OP_EQ:  // 等于
                return cmp == 0;
            case OP_NE:  // 不等于
                return cmp != 0;
            case OP_LT:  // 小于
                return cmp < 0;
            case OP_GT:  // 大于
                return cmp > 0;
            case OP_LE:  // 小于等于
                return cmp <= 0;
            case OP_GE:  // 大于等于
                return cmp >= 0;
            default:
                throw InternalError("Unexpected operator");
        }
    }

    bool eval_conds(const RmRecord *lhs_rec, const RmRecord *rhs_rec, const std::vector<Condition> &conds, const std::vector<ColMeta> &rec_cols) {
        return std::all_of(conds.begin(), conds.end(),
            [&](const Condition &cond) { return eval_cond(lhs_rec, rhs_rec, cond, rec_cols); }
        );
    }
};