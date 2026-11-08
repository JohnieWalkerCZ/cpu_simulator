// Tests for duplicating configurator items (src/ui/duplicate.hpp):
// unique copy naming (first/second copy, gaps, a base that already
// ends in " copy") and deep-copy independence (mutating the copy
// never touches the original).
#include "ui/duplicate.hpp"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using nlohmann::json;

int main() {
    // First copy, second copy, third copy.
    assert(UniqueCopyName({}, "ADD") == "ADD copy");
    assert(UniqueCopyName({"OTHER"}, "ADD") == "ADD copy");
    assert(UniqueCopyName({"ADD copy"}, "ADD") == "ADD copy 2");
    assert(UniqueCopyName({"ADD copy", "ADD copy 2"}, "ADD") ==
           "ADD copy 3");
    assert(UniqueCopyName({"ADD copy", "ADD copy 2", "ADD copy 3"},
                          "ADD") == "ADD copy 4");
    // A gap is reused: the lowest free number wins.
    assert(UniqueCopyName({"ADD copy", "ADD copy 3"}, "ADD") ==
           "ADD copy 2");
    // Unrelated names do not block the copy name.
    assert(UniqueCopyName({"ADD copy 2"}, "ADD") == "ADD copy");
    // A base that already ends in " copy" is treated literally:
    // duplicating "ADD copy" yields "ADD copy copy".
    assert(UniqueCopyName({"ADD copy"}, "ADD copy") == "ADD copy copy");
    assert(UniqueCopyName({"ADD copy", "ADD copy copy"}, "ADD copy") ==
           "ADD copy copy 2");
    // Empty base still produces a usable name.
    assert(UniqueCopyName({}, "") == " copy");

    // DuplicateItem: name replaced, everything else preserved.
    {
        json orig = json::parse(
            R"({"name": "ADD", "code": 1, "expression": "a + b",
                "latency": 1,
                "flag_rules": {"Z": "result_zero"}})");
        json copy = DuplicateItem(orig, "ADD copy");
        assert(copy["name"] == "ADD copy");
        assert(copy["code"] == 1);
        assert(copy["expression"] == "a + b");
        assert(copy["latency"] == 1);
        assert(copy["flag_rules"] == json::parse(R"({"Z": "result_zero"})"));
        // The original is untouched.
        assert(orig["name"] == "ADD");
    }

    // Deep-copy independence: mutating the copy (scalar, nested
    // object, nested array) never touches the original. This is what
    // makes duplicating an instruction safe: editing the copy's
    // microcode must not rewrite the original's.
    {
        json orig = json::parse(
            R"({"name": "ADD", "opcode": 5, "encoding": [5, "dest", "src"],
                "microcode": [{"action": "alu", "op": "ADD", "a": "@src",
                               "out": "@dest"}]})");
        json copy = DuplicateItem(orig, "ADD copy");
        copy["opcode"] = 9; // what the UI does next, via FirstFreeCode
        copy["encoding"][0] = 9;
        copy["microcode"][0]["op"] = "SUB";
        copy["microcode"][0]["a"] = "@dest";
        copy["microcode"].push_back({{"action", "halt"}});
        assert(orig["name"] == "ADD");
        assert(orig["opcode"] == 5);
        assert(orig["encoding"] == json::parse(R"([5, "dest", "src"])"));
        assert(orig["microcode"].size() == 1);
        assert(orig["microcode"][0]["op"] == "ADD");
        assert(orig["microcode"][0]["a"] == "@src");
        assert(copy["name"] == "ADD copy");
        assert(copy["opcode"] == 9);
        assert(copy["microcode"].size() == 2);
    }

    // An item without a "name" key gains one; other fields survive.
    {
        json orig = json::parse(R"({"width": 8, "initial": 0})");
        json copy = DuplicateItem(orig, "R0 copy");
        assert(copy["name"] == "R0 copy");
        assert(copy["width"] == 8);
        assert(!orig.contains("name"));
    }

    // Non-object items are returned as an unchanged deep copy.
    {
        json arr = json::parse(R"([1, 2, 3])");
        json copy = DuplicateItem(arr, "X copy");
        assert(copy == arr);
        copy.push_back(4);
        assert(arr.size() == 3);
    }

    std::cout << "All duplicate tests passed!" << std::endl;
    return 0;
}
