#include "BranchingTree.hpp"
#include "PointCloud.hpp"

#include <numeric>
#include <optional>
#include <algorithm>

#include "libslic3r/SLA/SupportTreeUtils.hpp"

namespace Slic3r { namespace branchingtree {

bool build_tree(PointCloud &nodes, Builder &builder)
{
    constexpr size_t ReachablesToExamine = 5;

    auto ptsqueue = nodes.start_queue();
    auto &properties = nodes.properties();

    struct NodeDistance { size_t node_id = Node::ID_NONE; float distance = NaNf; };
    auto distances = reserve_vector<NodeDistance>(ReachablesToExamine);
    double prev_dist_max = 0.;

    while (!ptsqueue.empty()) {
        size_t node_id = ptsqueue.top();

        Node node = nodes.get(node_id);
        nodes.mark_unreachable(node_id);

        distances.clear();

        nodes.foreach_reachable<ReachablesToExamine>(
            node.pos,
            [&distances](size_t id, float distance) {
                distances.emplace_back(NodeDistance{id, distance});
            },
            prev_dist_max);

        std::sort(distances.begin(), distances.end(),
                  [](auto &a, auto &b) { return a.distance < b.distance; });

        if (distances.empty()) {
            builder.report_unroutable(node);
            ptsqueue.pop();
            prev_dist_max = 0.;
            continue;
        }

        prev_dist_max = distances.back().distance;

        auto closest_it = distances.begin();
        bool routed = false;
        while (closest_it != distances.end() && !routed) {
            size_t closest_node_id = closest_it->node_id;
            Node closest_node = nodes.get(closest_node_id);

            auto type = nodes.get_type(closest_node_id);
            float w = nodes.get(node_id).weight + closest_it->distance;
            closest_node.Rmin = std::max(node.Rmin, closest_node.Rmin);

            switch (type) {
            case BED: {
                closest_node.weight = w;
                if (closest_it->distance > nodes.properties().max_branch_length()) {
                    auto hl_br_len = float(nodes.properties().max_branch_length()) / 2.f;
                    Node new_node {{node.pos.x(), node.pos.y(), node.pos.z() - hl_br_len}, node.Rmin};
                    new_node.id = int(nodes.next_junction_id());
                    new_node.weight = nodes.get(node_id).weight + nodes.properties().max_branch_length();
                    new_node.left = node.id;
                    if ((routed = builder.add_bridge(node, new_node))) {
                        size_t new_idx = nodes.insert_junction(new_node);
                        ptsqueue.push(new_idx);
                    }
                }
                else if ((routed = builder.add_ground_bridge(node, closest_node))) {
                    closest_node.left = closest_node.right = node_id;
                    nodes.get(closest_node_id) = closest_node;
                    nodes.mark_unreachable(closest_node_id);
                }

                break;
            }
            case MESH: {
                closest_node.weight = w;
                if ((routed = builder.add_mesh_bridge(node, closest_node))) {
                    closest_node.left = closest_node.right = node_id;
                    nodes.get(closest_node_id) = closest_node;
                    nodes.mark_unreachable(closest_node_id);
                }

                break;
            }
            case LEAF:
            case JUNCTION: {
                auto max_slope = float(properties.max_slope());

                if (auto mergept = find_merge_pt(node.pos, closest_node.pos, max_slope)) {

                    float mergedist_closest = (*mergept - closest_node.pos).norm();
                    float mergedist_node = (*mergept - node.pos).norm();
                    float Wnode = nodes.get(node_id).weight;
                    float Wclosest = nodes.get(closest_node_id).weight;
                    float Wsum = std::max(Wnode, Wclosest);
                    float distsum = std::max(mergedist_closest, mergedist_node);
                    w = Wsum + distsum;

                    if (mergedist_closest > EPSILON && mergedist_node > EPSILON) {
                        Node mergenode{*mergept, closest_node.Rmin};
                        mergenode.weight = w;
                        mergenode.id = int(nodes.next_junction_id());

                        if ((routed = builder.add_merger(node, closest_node, mergenode))) {
                            mergenode.left = node_id;
                            mergenode.right = closest_node_id;
                            size_t new_idx = nodes.insert_junction(mergenode);
                            ptsqueue.push(new_idx);
                            size_t qid = nodes.get_queue_idx(closest_node_id);

                            if (qid != PointCloud::Unqueued)
                                ptsqueue.remove(nodes.get_queue_idx(closest_node_id));

                            nodes.mark_unreachable(closest_node_id);
                        }
                    } else if (closest_node.pos.z() < node.pos.z() &&
                               (closest_node.left == Node::ID_NONE ||
                                closest_node.right == Node::ID_NONE)) {
                        closest_node.weight = w;
                        if ((routed = builder.add_bridge(node, closest_node))) {
                            if (closest_node.left == Node::ID_NONE)
                                closest_node.left = node_id;
                            else if (closest_node.right == Node::ID_NONE)
                                closest_node.right = node_id;

                            nodes.get(closest_node_id) = closest_node;
                        }
                    }
                }

                break;
            }
            case NONE:;
            }

            ++closest_it;
        }

        if (routed) {
            ptsqueue.pop();
            prev_dist_max = 0.;
        }

    }

    return true;
}

bool build_tree(const indexed_triangle_set & its,
                const std::vector<Node> &support_roots,
                Builder &                    builder,
                const Properties &           properties)
{
    PointCloud nodes(its, support_roots, properties);

    return build_tree(nodes, builder);
}

ExPolygon make_bed_poly(const indexed_triangle_set &its)
{
    auto bb = bounding_box(its);

    BoundingBox bbcrd{scaled(to_2d(bb.min)), scaled(to_2d(bb.max))};
    bbcrd.offset(scaled(10.));
    Point     min = bbcrd.min, max = bbcrd.max;
    ExPolygon ret = {{min.x(), min.y()},
                     {max.x(), min.y()},
                     {max.x(), max.y()},
                     {min.x(), max.y()}};

    return ret;
}

}} // namespace Slic3r::branchingtree
