#pragma once

#include <map>
#include <vector>

namespace oniku {

class Graph;
class Node;
class Value;

bool AddGradientForNode(Graph* graph, Graph* dest_graph, Node* node, std::map<Value*, Value*>* retained);

}  // namespace oniku
