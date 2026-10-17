#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace legacy {
struct Node {
    std::string class_name;
    std::unordered_map<std::string, std::string> texts;
    std::unordered_map<std::string, double> numbers;
    std::unordered_map<std::string, Node *> refs;
    std::unordered_map<std::string, std::vector<Node *>> lists;
    std::vector<double> values;
};
class Reader {
  public:
    explicit Reader(std::vector<uint8_t> bytes);
    std::vector<uint8_t> data;
    size_t pos = 0;
    std::vector<std::unique_ptr<Node>> nodes;
    std::unordered_map<std::string, uint32_t> versions;
    std::unordered_map<std::string, std::function<void(Reader &, Node &)>> handlers;
    uint8_t u8();
    uint16_t u16();
    uint32_t u32();
    double f64();
    std::vector<uint8_t> take(size_t n);
    void skip(size_t n);
    std::string string();
    uint32_t version(const std::string &name) const;
    uint32_t count(uint32_t limit = 1000000);
    Node *make_node(const std::string &name);
    Node *obj();
    void entity(Node &);
    void drawing(Node &);
    Node *typed_value(unsigned depth = 0);
    void base_header();
    Node *root_prefix();
    void root_suffix(Node &root);

  private:
    struct Entry {
        bool is_class;
        std::string name;
        Node *object;
    };
    std::vector<Entry> table;
    unsigned depth = 0;
    bool version_map_seen = false;
    void require(size_t n) const;
    size_t allocation_budget = 0;
    void account(size_t bytes);
    void register_metadata();
    void register_suffix();
};
} // namespace legacy
