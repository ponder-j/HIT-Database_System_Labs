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

class SeqScanExecutor : public AbstractExecutor {
   private:
    std::string tab_name_;              // 表的名称
    std::vector<Condition> conds_;      // scan的条件
    RmFileHandle *fh_;                  // 表的数据文件句柄
    std::vector<ColMeta> cols_;         // scan后生成的记录的字段
    size_t len_;                        // scan后生成的每条记录的长度
    std::vector<Condition> fed_conds_;  // 同conds_，两个字段相同

    Rid rid_;
    std::unique_ptr<RecScan> scan_;     // table_iterator

    SmManager *sm_manager_;

   public:
    SeqScanExecutor(SmManager *sm_manager, std::string tab_name, std::vector<Condition> conds, Context *context) {
        sm_manager_ = sm_manager;
        tab_name_ = std::move(tab_name);
        conds_ = std::move(conds);
        TabMeta &tab = sm_manager_->db_.get_table(tab_name_);
        fh_ = sm_manager_->fhs_.at(tab_name_).get();
        cols_ = tab.cols;
        len_ = cols_.back().offset + cols_.back().len;

        context_ = context;

        fed_conds_ = conds_;
    }

    /**
     * @brief 构建表迭代器scan_,并开始迭代扫描,直到扫描到第一个满足谓词条件的元组停止,并赋值给rid_
     *
     */
    void beginTuple() override {
        // 创建表的记录扫描迭代器
        scan_ = std::make_unique<RmScan>(fh_);
        
        // 迭代扫描表中的元组，直到找到第一条满足所有条件的元组
        while (!scan_->is_end()) {
            rid_ = scan_->rid();  // 获取当前记录的Rid
            // 获取当前记录
            auto rec = fh_->get_record(rid_, context_);
            // 检查该记录是否满足所有选择条件
            if (eval_conds(rec.get(), fed_conds_, cols_)) {
                break;  // 找到满足条件的元组，停止扫描
            }
            scan_->next();  // 移动到下一条记录
        }
    }

    /**
     * @brief 从当前scan_指向的记录开始迭代扫描,直到扫描到第一个满足谓词条件的元组停止,并赋值给rid_
     *
     */
    void nextTuple() override {
        // 确保scan_已经初始化
        assert(scan_ != nullptr);
        
        // 移动到下一条记录
        scan_->next();
        
        // 继续扫描，直到找到满足条件的元组或到达末尾
        while (!scan_->is_end()) {
            rid_ = scan_->rid();  // 获取当前记录的Rid
            // 获取当前记录
            auto rec = fh_->get_record(rid_, context_);
            // 检查该记录是否满足所有选择条件
            if (eval_conds(rec.get(), fed_conds_, cols_)) {
                break;  // 找到满足条件的元组，停止扫描
            }
            scan_->next();  // 移动到下一条记录
        }
    }

    /**
     * @brief 返回下一个满足扫描条件的记录
     *
     * @return std::unique_ptr<RmRecord>
     */
    std::unique_ptr<RmRecord> Next() override {
        // 返回rid_标识的记录
        return fh_->get_record(rid_, context_);
    }

    Rid &rid() override { return rid_; }

    bool is_end() const override { 
        // 判断scan_是否已经到达末尾
        return scan_->is_end(); 
    }
    
    std::string getType() override { return "SeqScanExecutor"; }

    size_t tupleLen() const override { return len_; }

    const std::vector<ColMeta> &cols() const override { return cols_; }

    private:
    bool eval_cond(const RmRecord *rec, const Condition &cond, const std::vector<ColMeta> &rec_cols) {
        // 获取左部表达式的列元数据和值
        auto lhs_col = get_col(rec_cols, cond.lhs_col);
        char *lhs_data = rec->data + lhs_col->offset;  // 左部值的起始地址
        
        char *rhs_data;  // 右部值的起始地址
        ColType rhs_type;
        int rhs_len;
        
        // 判断右部是值还是列
        if (cond.is_rhs_val) {
            // 右部是常量值
            rhs_type = cond.rhs_val.type;
            rhs_len = lhs_col->len;  // 使用左部列的长度
            rhs_data = cond.rhs_val.raw->data;
        } else {
            // 右部是列
            auto rhs_col = get_col(rec_cols, cond.rhs_col);
            rhs_type = rhs_col->type;
            rhs_len = rhs_col->len;
            rhs_data = rec->data + rhs_col->offset;
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

    bool eval_conds(const RmRecord *rec, const std::vector<Condition> &conds, const std::vector<ColMeta> &rec_cols) {
        return std::all_of(conds.begin(), conds.end(),
            [&](const Condition &cond) { return eval_cond(rec, cond, rec_cols); }
        );
    }
};