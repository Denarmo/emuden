// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// xdelta3's FGK sections: adaptive Huffman coding (Faller, Gallager, Knuth). Nothing but the
// bytes is written; coder and decoder grow the same tree, so the decoder has to change its
// tree exactly as xdelta3 does. The tree's rules below follow xdelta3-fgk.h (Joshua
// MacDonald, Apache-2.0). The tree is kept from window to window.
//
// A byte seen before is the path to its leaf, from the root (1 = right). A byte seen for the
// first time is the path to the leaf that stands for all unseen bytes, then its position in
// the list of unseen bytes (as many bits as that list needs, low bit first).

#include <algorithm>
#include <array>
#include <utility>
#include <vector>

#include "secondary.h"

namespace den::patching {

namespace {

constexpr std::size_t SYMBOLS = 256;

struct Node;
// Nodes of one weight form a block; it knows its rightmost node, the one that swaps places
// with a node whose weight is about to grow. A free block is a link of the free list.
struct Block {
    Node* leader = nullptr;
    Block* next_free = nullptr;
};

// `left` and `right` order all nodes by weight. For a byte not seen yet, `left_child` and
// `right_child` are instead its neighbours in the list of unseen bytes.
struct Node {
    std::uint32_t weight = 0;
    Node* parent = nullptr;
    Node* left_child = nullptr;
    Node* right_child = nullptr;
    Node* left = nullptr;
    Node* right = nullptr;
    Block* block = nullptr;
};

class FgkDecoder final : public Secondary {
public:
    FgkDecoder() : nodes_(2 * SYMBOLS - 1), blocks_(2 * (2 * SYMBOLS - 1)) {
        root_ = at_ = unseen_ = nodes_.data();
        next_node_ = nodes_.data() + SYMBOLS;
        unseen_count_ = SYMBOLS + 2;
        CountDown();
        CountDown();
        for (std::size_t i = 0; i + 1 < blocks_.size(); ++i) {
            blocks_[i].next_free = &blocks_[i + 1];
        }
        free_block_ = blocks_.data();
        for (std::size_t i = 0; i < SYMBOLS; ++i) {
            nodes_[i].right_child = i + 1 < SYMBOLS ? &nodes_[i + 1] : nullptr;
            nodes_[i].left_child = i >= 1 ? &nodes_[i - 1] : nullptr;
        }
    }

    bool Decode(Cursor& in, std::uint8_t* out, std::size_t size) override {
        std::size_t done = 0;
        while (done < size) {
            if (in.Left() == 0) {
                return false;
            }
            const unsigned byte = in.U8();
            // The bits after a section's last byte are padding.
            for (unsigned mask = 1; mask != 0x100 && done < size; mask <<= 1) {
                if (Bit((byte & mask) != 0)) {
                    out[done++] = static_cast<std::uint8_t>(Finish());
                }
            }
        }
        return true;
    }

private:
    // One bit further; true when a byte is complete.
    bool Bit(bool bit) {
        if (at_->weight == 0) {
            if (position_bits_ < position_.size()) {
                position_[position_bits_] = bit;
            }
            ++position_bits_;
            return position_bits_ >= (unseen_extra_ == 0 ? unseen_log_ : unseen_log_ + 1);
        }
        at_ = bit ? at_->right_child : at_->left_child;
        if (at_ == nullptr) {
            at_ = root_; // cannot happen in a tree built by Update
            return false;
        }
        if (at_->left_child != nullptr) {
            return false;
        }
        return at_->weight != 0 || unseen_count_ == 1;
    }

    std::size_t Finish() {
        std::size_t symbol = static_cast<std::size_t>(at_ - nodes_.data());
        if (at_->weight == 0) {
            std::size_t n = 0;
            const std::size_t count = std::min(position_bits_, position_.size());
            for (std::size_t i = 0; i < count; ++i) {
                n = (n << 1) | (position_[i] ? 1 : 0);
            }
            const Node* node = unseen_;
            for (; n != 0 && node->right_child != nullptr; --n) {
                node = node->right_child;
            }
            symbol = static_cast<std::size_t>(node - nodes_.data());
        }
        position_bits_ = 0;
        Update(symbol);
        at_ = root_;
        return symbol;
    }

    // The byte was seen once more: every node from its leaf to the root gains one, after
    // changing places with the leader of its block so that the weights stay ordered.
    void Update(std::size_t symbol) {
        Node* node = nodes_[symbol].weight == 0 ? FirstSeen(symbol) : &nodes_[symbol];
        while (node != root_) {
            MoveRight(node);
            Promote(node);
            ++node->weight;
            node = node->parent;
        }
        ++root_->weight;
    }

    void MoveRight(Node* forward) {
        Node* back = forward->block->leader;
        if (forward == back || forward->parent == back || forward->weight == 0) {
            return;
        }
        back->right->left = forward;
        if (forward->left != nullptr) {
            forward->left->right = back;
        }
        Node* other = forward->right;
        forward->right = back->right;
        if (other == back) {
            back->right = forward;
        } else {
            other->left = back;
            back->right = other;
        }
        other = back->left;
        back->left = forward->left;
        if (other == forward) {
            forward->left = back;
        } else {
            other->right = forward;
            forward->left = other;
        }
        Node** forward_slot =
            forward->parent->right_child == forward ? &forward->parent->right_child : &forward->parent->left_child;
        Node** back_slot = back->parent->right_child == back ? &back->parent->right_child : &back->parent->left_child;
        std::swap(forward->parent, back->parent);
        std::swap(*forward_slot, *back_slot);
        forward->block->leader = forward;
    }

    // Moves the leader of a block into the block of the next weight.
    void Promote(Node* node) {
        Node* right = node->right;
        Node* left = node->left;
        Block* block = node->block;
        if (node->weight == 0) {
            return;
        }
        // The parent of the unseen leaf has the weight of its other child.
        if (left == node->right_child && node->left_child != nullptr && node->left_child->weight == 0) {
            if (node->weight == right->weight - 1 && right != root_) {
                Free(block);
                node->block = right->block;
                left->block = right->block;
            }
            return;
        }
        if (left == unseen_) {
            return;
        }
        if (left->block == block) {
            left->block->leader = left;
        } else {
            Free(block);
        }
        node->block = node->weight == right->weight - 1 && right != root_ ? right->block : Make(node);
    }

    // Takes the byte out of the unseen list and hangs it, with a new inner node, where the
    // unseen leaf was.
    Node* FirstSeen(std::size_t symbol) {
        Node* seen = &nodes_[symbol];
        if (unseen_count_ == 1) {
            seen->right_child = nullptr;
            seen->block = seen->right->weight == 1 ? seen->right->block : Make(seen);
            unseen_ = nullptr;
            return seen;
        }
        Node* zero = unseen_;
        Node* inner = next_node_++;
        inner->parent = zero->parent;
        inner->right = zero->right;
        inner->weight = 0;
        inner->right_child = seen;
        inner->left = seen;
        if (unseen_ == root_) {
            root_ = inner;
            seen->block = Make(seen);
            inner->block = Make(inner);
        } else {
            inner->right->left = inner;
            if (zero->parent->right_child == zero) {
                zero->parent->right_child = inner;
            } else {
                zero->parent->left_child = inner;
            }
            inner->block = inner->right->weight == 1 ? inner->right->block : Make(inner);
            seen->block = inner->block;
        }
        // Out of the list.
        CountDown();
        if (seen->left_child == nullptr) {
            unseen_ = unseen_->right_child;
            unseen_->left_child = nullptr;
        } else if (seen->right_child == nullptr) {
            seen->left_child->right_child = nullptr;
        } else {
            seen->right_child->left_child = seen->left_child;
            seen->left_child->right_child = seen->right_child;
        }
        inner->left_child = unseen_;
        seen->right = inner;
        seen->left = unseen_;
        seen->parent = inner;
        seen->left_child = nullptr;
        seen->right_child = nullptr;
        unseen_->parent = inner;
        unseen_->right = seen;
        return seen;
    }

    Block* Make(Node* leader) {
        Block* block = free_block_;
        free_block_ = block->next_free;
        block->leader = leader;
        return block;
    }
    void Free(Block* block) {
        block->next_free = free_block_;
        free_block_ = block;
    }

    // One unseen byte fewer: the count as 2^log + extra.
    void CountDown() {
        std::size_t left = --unseen_count_;
        unseen_log_ = 0;
        while (left > 1) {
            ++unseen_log_;
            left >>= 1;
        }
        unseen_extra_ = unseen_count_ - (std::size_t{1} << unseen_log_);
    }

    std::vector<Node> nodes_;
    std::vector<Block> blocks_;
    Node* root_ = nullptr;
    Node* at_ = nullptr;
    Node* unseen_ = nullptr;
    Node* next_node_ = nullptr;
    Block* free_block_ = nullptr;
    std::size_t unseen_count_ = 0, unseen_log_ = 0, unseen_extra_ = 0;
    std::array<bool, 16> position_{};
    std::size_t position_bits_ = 0;
};

} // namespace

std::unique_ptr<Secondary> MakeFgkDecoder() {
    return std::make_unique<FgkDecoder>();
}

} // namespace den::patching
