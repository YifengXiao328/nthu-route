#include "Layerassignment.h"

#include <boost/multi_array/subarray.hpp>
#include <boost/range/combine.hpp>
#include <sys/time.h>
#include <unistd.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <functional>
#include <queue>
#include <unordered_map>

#include "../grdb/EdgePlane.h"
#include "../grdb/RoutingComponent.h"
#include "../grdb/RoutingRegion.h"
#include "Congestion.h"
#include "Construct_2d_tree.h"

using namespace std;

char prefer_direction[6][2] = { 0 };

Edge_3d::Edge_3d() :
        max_cap(0), cur_cap(0), used_net(5) {

}

void Layer_assignment::print_max_overflow() {
    int lines = 0;
    int max = 0;
    int sum = 0;
    for (Edge_3d& edgeLeft : cur_map_3d.all()) {

        if (edgeLeft.isOverflow()) {
            max = std::max(edgeLeft.overUsage(), max);
            sum += edgeLeft.overUsage();
            ++lines;
        }
    }

    printf("3D # of overflow = %d\n", sum);
    printf("3D max overflow = %d\n", max);
    printf("3D overflow edge number = %d\n", lines);
}

void Layer_assignment::initial_overflow_map() {

    for (auto pair : boost::combine(congestion.congestionMap2d.all(), layerInfo_map.edges().all())) {
        pair.get<1>().overflow = pair.get<0>().overUsage() * 2;
    }
}

void Layer_assignment::malloc_space() {

    for (LayerInfo& layerInfo : layerInfo_map.allVertex()) {
        layerInfo.path = 0;  // non-visited
    }
    for (EdgeInfo& edge : layerInfo_map.edges().all()) {
        edge.overflow = 0;
        edge.path = 0;
    }

    for (Edge_3d& edge : cur_map_3d.all()) {

        edge.viadensity.cur = 0;
        edge.viadensity.max = 0;
    }
}

void Layer_assignment::update_cur_map_for_klat_xy(int cur_idx, const Coordinate_2d& start, const Coordinate_2d& end, int net_id) {

    Edge_3d& edge = cur_map_3d.edge(Coordinate_3d { end, cur_idx }, Coordinate_3d { start, cur_idx });
    ++edge.used_net[net_id];
    edge.cur_cap = (edge.used_net.size() * 2);	// need check
    if (edge.isOverflow()) {	// need check
        layerInfo_map.edges().edge(start, end).overflow -= 2;
    }
}

void Layer_assignment::update_cur_map_for_klat_z(int min, int max, const Coordinate_2d& start, int net_id) {

    Coordinate_3d previous { start, min };
    for (Coordinate_3d k { start, min + 1 }; k.z < max; ++k.z) {
        Edge_3d& edge = cur_map_3d.edge(k, previous);
        ++edge.used_net[net_id];
        ++edge.viadensity.cur;
        previous = k;
    }

}

void Layer_assignment::update_path_for_klat(const Coordinate_2d& start) {
    int pin_num = 0;
    std::queue<Coordinate_3d> q;

// BFS
    q.push(Coordinate_3d { start, 0 });	// enqueue
    while (!q.empty()) {
        Coordinate_3d& head3d = q.front();
        Coordinate_2d h = head3d.xy();
        int z_min = 0;
        if (layerInfo_map.vertex(h).path == 2) {	// a pin
            ++pin_num;
        } else {
            z_min = head3d.z;
        }
        int z_max = head3d.z;
        for (EdgePlane<EdgeInfo>::Handle& handle : layerInfo_map.edges().neighbors(h)) {
            if (handle.edge().path == 1) {	// check legal
                Coordinate_2d& c = handle.vertex();
                if (layerInfo_map.vertex(c).path == 0) {
                    puts("ERROR");
                }
                int pi_z = layerInfo_map.vertex(c).zLayerInfo[head3d.z].klat.pi_z;
                z_max = std::max(z_max, pi_z);
                z_min = std::min(z_min, pi_z);
                update_cur_map_for_klat_xy(pi_z, h, c, global_net_id);
                q.push(Coordinate_3d { c, pi_z });	// enqueue
            }
        }
        update_cur_map_for_klat_z(z_min, z_max, h, global_net_id);
        layerInfo_map.vertex(h).path = 0;	// visited
        q.pop();	// dequeue
    }
    if (pin_num != global_pin_num)
        printf("net : %d, pin number error %d vs %d\n", global_net_id, pin_num, global_pin_num);
}

void Layer_assignment::cycle_reduction(const Coordinate_2d& c, const Coordinate_2d& parent) {

    for (EdgePlane<EdgeInfo>::Handle& handle : layerInfo_map.edges().neighbors(c)) {
        if (parent != handle.vertex() && handle.edge().path == 1) {   // check legal
            cycle_reduction(handle.vertex(), c);
        }
    }
    // border effect, we need to loop again
    bool isLeaf = true;
    for (EdgePlane<EdgeInfo>::Handle& handle : layerInfo_map.edges().neighbors(c)) {
        if (handle.edge().path == 1) {   // check legal
            isLeaf = false;
        }
    }
    if (isLeaf && layerInfo_map.vertex(c).path == 1) {	// a leaf && it is not a pin
        layerInfo_map.vertex(c).path = 0;
        for (EdgePlane<EdgeInfo>::Handle& handle : layerInfo_map.edges().neighbors(c)) {
            handle.edge().path = 0;
        }
    }
}

void Layer_assignment::preprocess(int net_id) {

    const PinptrList& pin_list = construct_2d_tree.rr_map.get_nPin(net_id);

    for (const Pin* pin : pin_list) {
        layerInfo_map.vertex(pin->get_tileXY()).path = -2;	// pin
    }
// BFS speed up
    Coordinate_2d c = pin_list[0]->get_tileXY();
    layerInfo_map.vertex(c).path = 2;	// visited

    // initial klat_map[x][y][k]
    for (ZLayerInfo& k : layerInfo_map.vertex(c).zLayerInfo) {
        k.klat.val = -1;
    }

    std::queue<ElementQueue> q;
    q.emplace(c, c);	// enqueue
    while (!q.empty()) {
        ElementQueue& front = q.front();
        for (EdgePlane<EdgeInfo>::Handle& handle : layerInfo_map.edges().neighbors(front.coor)) {
            if (handle.vertex() != front.parent) {
                if (congestion.congestionMap2d.edge(front.coor, handle.vertex()).lookupNet(net_id)) {	//
                    char& pathV = layerInfo_map.vertex(handle.vertex()).path;
                    if (pathV == 0 || pathV == -2) {

                        handle.edge().path = 1;
                        if (pathV == 0) {
                            pathV = 1;	// visited node
                        } else {	// path_map[x][y].val == 2
                            pathV = 2;	// visited pin

                        }
                        // initial klat_map[x][y][k]
                        for (ZLayerInfo& k : layerInfo_map.vertex(handle.vertex()).zLayerInfo) {
                            k.klat.val = -1;
                        }
                        q.emplace(handle.vertex(), front.coor);	// enqueue coor + parent
                    } else {
                        // already visited : we cut
                        handle.edge().path = 0;
                    }
                } else {
                    //nothing to see here
                    handle.edge().path = 0;
                }
            }
        }
        q.pop();	// dequeue
    }
    cycle_reduction(c, c);
}

void Layer_assignment::rec_count(int level, int val, std::array<int, 4>& count_idx) {
// level is orientation
    if (level < 4) {
        if (val <= min_DP_val) {
            std::array<int, 4> temp_idx = count_idx;

            if (path_map[global_x][global_y].edge[level] == 1) {
                int temp_x = global_x + plane_dir[level][0];
                int temp_y = global_y + plane_dir[level][1];
                for (k = 0; k < global_max_layer; ++k)	// use global_max_layer to substitue max_zz
                    if (klat_map[temp_x][temp_y][k].val >= 0) {
                        temp_idx[level] = k;
                        rec_count(level + 1, val + klat_map[temp_x][temp_y][k].val, temp_idx);
                    }
            } else {
                temp_idx[level] = -1;
                rec_count(level + 1, val, temp_idx);
            }
        }
    } else	// level == 4
    {
        int min_k = min_DP_k;
        int max_k = max_DP_k;
        int temp_via_cost = 0;
        for (int k = temp_via_cost = 0; k < 4; ++k)
            if (count_idx[k] != -1) {
                if (count_idx[k] < min_k)
                    min_k = count_idx[k];
                if (count_idx[k] > max_k)
                    max_k = count_idx[k];
                temp_via_cost += klat_map[global_x + plane_dir[k][0]][global_y + plane_dir[k][1]][count_idx[k]].via_cost;
            }
        temp_via_cost += ((max_k - min_k) * congestion.via_cost);

        int temp_val = val;
        for (int k = min_k; k < max_k; ++k)
            if (viadensity_map[global_x][global_y][k].cur >= viadensity_map[global_x][global_y][k].max)
                ++temp_val;

        if (temp_val < min_DP_val) {
            min_DP_val = temp_val;
            min_DP_via_cost = temp_via_cost;
            for (int k = 0; k < 4; ++k)
                min_DP_idx[k] = count_idx[k];
        } else if (temp_val == min_DP_val) {

            if (temp_via_cost < min_DP_via_cost) {
                min_DP_via_cost = temp_via_cost;
                for (int k = 0; k < 4; ++k)
                    min_DP_idx[k] = count_idx[k];
            } else if (temp_via_cost == min_DP_via_cost) {
                int temp_min_k = min_DP_k;
                int temp_max_k = max_DP_k;
                for (k = 0; k < 4; ++k)
                    if (min_DP_idx[k] != -1) {
                        if (min_DP_idx[k] < temp_min_k)
                            temp_min_k = min_DP_idx[k];
                        if (min_DP_idx[k] > temp_max_k)
                            temp_max_k = min_DP_idx[k];
                    }
                if (max_k > temp_max_k || min_k > temp_min_k)
                    for (int k = 0; k < 4; ++k)
                        min_DP_idx[k] = count_idx[k];
            }

        }
    }
}

void Layer_assignment::DP(int x, int y, int z) {
    int i, k, temp_x, temp_y;
    bool is_end;
    std::array<int, 4> count_idx;

    Coordinate c1(x, y);

    if (klat_map[x][y][z].val == -1) {	// non-traversed

        is_end = true;
        for (Coordinate_2d cr : plane_dir) {	// direction

            if (path_map[x][y].edge[i] == 1) {	// check legal

                is_end = false;
                Coordinate_2d c2 = c1 + cr;
                if (*(overflow_map[x][y].edge[i]) <= 0)	// this edge could not happen overflow
                    for (k = 0; k < global_max_layer; ++k) {	// use global_max_layer to substitute max_zz

                        Edge_3d& edge = cur_map_3d.edge(x, y, k, i);
                        if (edge.cur_cap < cur_map_3d[x][y][k].edge_list[i]->max_cap) {	// pass without overflow
                            DP(temp_x, temp_y, k);
                        }
                    }
                else
                    // this edge could happen overflow
                    for (k = 0; k < global_max_layer; ++k) {	// use global_max_layer to substitute max_zz

                        if ((cur_map_3d.edge(x, y, k, i).cur_cap - cur_map_3d[x][y][k].edge_list[i]->max_cap) < overflow_max)	// overflow, but doesn't over the overflow_max

                            DP(temp_x, temp_y, k);

                    }
            }
        }
        if (is_end == true) {
            klat_map[x][y][z].via_cost = construct_2d_tree.via_cost * z;

            for (k = klat_map[x][y][z].via_overflow = 0; k < z; ++k)
                if (viadensity_map[x][y][k].cur >= viadensity_map[x][y][k].max)
                    ++klat_map[x][y][z].via_overflow;
            klat_map[x][y][z].val = klat_map[x][y][z].via_overflow;

        } else {	// is_end == false

            // special purpose
            min_DP_val = 1000000000;
            min_DP_via_cost = 1000000000;
            global_x = x;
            global_y = y;
            max_DP_k = z;
            if (path_map[x][y].val == 1)	// not a pin
                min_DP_k = z;
            else
                // pin
                min_DP_k = 0;
            for (i = 0; i < 4; ++i)
                min_DP_idx[i] = -1;
            rec_count(0, 0, count_idx);
            klat_map[x][y][z].via_cost = min_DP_via_cost;

            klat_map[x][y][z].via_overflow = min_DP_val;

            klat_map[x][y][z].val = min_DP_val;
            for (i = 0; i < 4; ++i)	// direction
                if (min_DP_idx[i] >= 0) {
                    temp_x = x + plane_dir[i][0];
                    temp_y = y + plane_dir[i][1];
                    klat_map[temp_x][temp_y][z].pi_z = min_DP_idx[i];
                }
        }
    }
}

bool Layer_assignment::in_cube_and_have_edge(int x, int y, int z, int dir, int net_id) {
// F B L R U D
    int anti_dir[6] = { 1, 0, 3, 2, 5, 4 };
// B F R L D U

    if (x >= 0 && x < max_xx && y >= 0 && y < max_yy && z >= 0 && z < max_zz) {
        if (cur_map_3d[x][y][z].edge_list[anti_dir[dir]]->used_net.find(net_id) != cur_map_3d[x][y][z].edge_list[anti_dir[dir]]->used_net.end()) {
            return true;
        } else
            return false;
    } else
        return false;
}

bool Layer_assignment::have_child(int pre_x, int pre_y, int pre_z, int pre_dir, int net_id) {
    int dir, x, y, z;
    int anti_dir[6] = { 1, 0, 3, 2, 5, 4 };

    for (dir = 0; dir < 6; ++dir)
        if (dir != pre_dir && dir != anti_dir[pre_dir]) {
            x = pre_x + cube_dir[dir][0];
            y = pre_y + cube_dir[dir][1];
            z = pre_z + cube_dir[dir][2];
            if (in_cube_and_have_edge(x, y, z, dir, net_id) == true && BFS_color_map[x][y][z] != net_id)
                return true;
        }
    return false;
}

void Layer_assignment::generate_output(int net_id) {
    int i, j;

    RoutingRegion& rr_map = construct_2d_tree.rr_map;

    const PinptrList& pin_list = rr_map.get_nPin(net_id);
    const char *p;
    queue<Coordinate_3d> q;
    int dir;

    Coordinate_3d start;
    Coordinate_3d end;

    int xDetailShift = rr_map.get_llx() + (rr_map.get_tileWidth() >> 1);
    int yDetailShift = rr_map.get_lly() + (rr_map.get_tileHeight() >> 1);

// the beginning of a net of output file
    p = rr_map.get_netName(net_id);
    printf("%s", p);
    for (i = 0; p[i] && (p[i] < '0' || p[i] > '9'); ++i)
        ;
    printf(" %s\n", p + i);
// BFS
    q.push(Coordinate_3d { pin_list[0]->get_tileXY(), 0 });	// enqueue
    Coordinate_3d& temp = q.front();
    BFS_color_map[temp.x][temp.y][temp.z] = net_id;
    while (!q.empty()) {
        temp = q.front();
        for (dir = 0; dir < 6; dir += 2) {
            int dirPlusOne = dir + 1;
            start.x = end.x = temp.x;
            start.y = end.y = temp.y;
            start.z = end.z = temp.z;

            for (i = 1;; ++i) {
                start.x += cube_dir[dir][0];
                start.y += cube_dir[dir][1];
                start.z += cube_dir[dir][2];
                if (in_cube_and_have_edge(start.x, start.y, start.z, dir, net_id) == true && BFS_color_map[start.x][start.y][start.z] != net_id) {
                    BFS_color_map[start.x][start.y][start.z] = net_id;
                    if (have_child(start.x, start.y, start.z, dir, net_id) == true)
                        q.push(start);	// enqueue
                } else {
                    start.x -= cube_dir[dir][0];
                    start.y -= cube_dir[dir][1];
                    start.z -= cube_dir[dir][2];
                    break;
                }
            }
            for (j = 1;; ++j) {
                end.x += cube_dir[dirPlusOne][0];
                end.y += cube_dir[dirPlusOne][1];
                end.z += cube_dir[dirPlusOne][2];
                if (in_cube_and_have_edge(end.x, end.y, end.z, dirPlusOne, net_id) == true && BFS_color_map[end.x][end.y][end.z] != net_id) {
                    BFS_color_map[end.x][end.y][end.z] = net_id;
                    if (have_child(end.x, end.y, end.z, dirPlusOne, net_id) == true)
                        q.push(end);	// enqueue
                } else {
                    end.x -= cube_dir[dirPlusOne][0];
                    end.y -= cube_dir[dirPlusOne][1];
                    end.z -= cube_dir[dirPlusOne][2];
                    break;
                }
            }
            if (i >= 2 || j >= 2)	// have edge
                    {

                start.x = start.x * rr_map.get_tileWidth() + xDetailShift;
                start.y = start.y * rr_map.get_tileHeight() + yDetailShift;
                ++start.z;
                end.x = end.x * rr_map.get_tileWidth() + xDetailShift;
                end.y = end.y * rr_map.get_tileHeight() + yDetailShift;
                ++end.z;
                printf("(%d,%d,%d)-(%d,%d,%d)\n", start.x, start.y, start.z, end.x, end.y, end.z);
            }
        }
        q.pop();
    }
// the end of a net of output file
    printf("!\n");
}

void Layer_assignment::klat(int net_id) { //SOLAC + APEC
    const PinptrList& pin_list = construct_2d_tree.rr_map.get_nPin(net_id);

    Coordinate_2d start = pin_list[0]->get_tileXY();
    global_net_id = net_id;	// LAZY global variable
    global_pin_num = construct_2d_tree.rr_map.get_netPinNumber(net_id);
    preprocess(net_id);
// fina a pin as starting point
// klat start with a pin
    DP(start.x, start.y, 0);

    global_pin_cost += klat_map[start.x][start.y][0].val;
    update_path_for_klat(start);
}

bool Layer_assignment::comp_temp_net_order(int p, int q) {

    return average_order[q].average < average_order[p].average || //
            (!(average_order[p].average < average_order[q].average) && //
                    construct_2d_tree.rr_map.get_netPinNumber(p) < construct_2d_tree.rr_map.get_netPinNumber(q));
}

int Layer_assignment::backtrace(int n) {
    if (group_set[n].pi != n) {
        group_set[n].pi = backtrace(group_set[n].pi);
        return group_set[n].pi;
    }
    return group_set[n].pi;
}

void Layer_assignment::find_group(int max) {
    int i, j, k;
    LRoutedNetTable::iterator iter, iter2;
    int a, b, pre_solve_counter = 0, temp_cap;
    std::deque<bool> pre_solve(max);
    int max_layer;

    group_set = std::vector<UNION_NODE>(max);

// intitial for is_used_for_BFS
    for (i = 0; i < max_xx; ++i)
        for (j = 0; j < max_yy; ++j)
            is_used_for_BFS[i][j] = -1;
// initial for average_order
    average_order = std::vector<AVERAGE_NODE>(max);
    for (i = 0; i < max; ++i) {
        group_set[i].pi = i;
        group_set[i].sx = group_set[i].sy = 1000000;
        group_set[i].bx = group_set[i].by = -1;
        group_set[i].num = 1;
        pre_solve[i] = true;
        average_order[i].id = i;
        average_order[i].val = 0;
        average_order[i].times = 0;
        average_order[i].vo_times = 0;
    }
    for (i = 1; i < max_xx; ++i)
        for (j = 0; j < max_yy; ++j) {
            Edge_2d& edgeWest = congestion.congestionMap2d.edge(i, j, DIR_WEST);
            temp_cap = (edgeWest.used_net.size() << 1);
            for (k = 0; k < max_zz && temp_cap > 0; ++k)
                if (cur_map_3d[i][j][k].edge_list[LEFT]->max_cap > 0)
                    temp_cap -= cur_map_3d[i][j][k].edge_list[LEFT]->max_cap;
            if (k == max_zz)
                max_layer = max_zz - 1;
            else
                max_layer = k;
            if (edgeWest.isOverflow() == false) {
                if (edgeWest.used_net.size() > 0) {
                    for (k = 0; k < max_zz && cur_map_3d[i][j][k].edge_list[LEFT]->max_cap == 0; ++k)
                        ;
                    if (k < max_zz) {
                        if (cur_map_3d[i][j][k].edge_list[LEFT]->max_cap < (int) (edgeWest.used_net.size() << 1))
                            for (RoutedNetTable::iterator iter = edgeWest.used_net.begin(); iter != edgeWest.used_net.end(); ++iter) {
                                pre_solve[iter->first] = false;
                            }
                    } else
                        for (RoutedNetTable::iterator iter = edgeWest.used_net.begin(); iter != edgeWest.used_net.end(); ++iter) {
                            pre_solve[iter->first] = false;
                        }
                }
            } else {
                for (RoutedNetTable::iterator iter = edgeWest.used_net.begin(); iter != edgeWest.used_net.end(); ++iter) {
                    pre_solve[iter->first] = false;
                }
            }
            for (RoutedNetTable::iterator iter = edgeWest.used_net.begin(); iter != edgeWest.used_net.end(); ++iter) {
                if (i - 1 < group_set[iter->first].sx)
                    group_set[iter->first].sx = i - 1;
                if (i > group_set[iter->first].bx)
                    group_set[iter->first].bx = i;
                if (j < group_set[iter->first].sy)
                    group_set[iter->first].sy = j;
                if (j > group_set[iter->first].by)
                    group_set[iter->first].by = j;
                average_order[iter->first].val += max_layer;
                ++average_order[iter->first].times;
            }
        }
    for (i = 0; i < max_xx; ++i)
        for (j = 1; j < max_yy; ++j) {
            Edge_2d& edgeSouth = construct_2d_tree.congestionMap2d.edge(i, j, DIR_SOUTH);
            temp_cap = (edgeSouth.used_net.size() << 1);
            for (k = 0; k < max_zz && temp_cap > 0; ++k)
                if (cur_map_3d[i][j][k].edge_list[BACK]->max_cap > 0)
                    temp_cap -= cur_map_3d[i][j][k].edge_list[BACK]->max_cap;
            if (k == max_zz)
                max_layer = max_zz - 1;
            else
                max_layer = k;
            if (edgeSouth.isOverflow() == false) {
                if (edgeSouth.used_net.size() > 0) {
                    for (k = 0; k < max_zz && cur_map_3d[i][j][k].edge_list[BACK]->max_cap == 0; ++k)
                        ;
                    if (k < max_zz) {
                        if (cur_map_3d[i][j][k].edge_list[BACK]->max_cap < (int) (edgeSouth.used_net.size() << 1))
                            for (RoutedNetTable::iterator iter = edgeSouth.used_net.begin(); iter != edgeSouth.used_net.end(); ++iter) {
                                pre_solve[iter->first] = false;
                            }
                    } else
                        for (RoutedNetTable::iterator iter = edgeSouth.used_net.begin(); iter != edgeSouth.used_net.end(); ++iter) {
                            pre_solve[iter->first] = false;
                        }
                }
            } else {
                for (RoutedNetTable::iterator iter = edgeSouth.used_net.begin(); iter != edgeSouth.used_net.end(); ++iter) {
                    pre_solve[iter->first] = false;
                }
            }
            for (RoutedNetTable::iterator iter = edgeSouth.used_net.begin(); iter != edgeSouth.used_net.end(); ++iter) {
                if (i < group_set[iter->first].sx)
                    group_set[iter->first].sx = i;
                if (i > group_set[iter->first].bx)
                    group_set[iter->first].bx = i;
                if (j - 1 < group_set[iter->first].sy)
                    group_set[iter->first].sy = j - 1;
                if (j > group_set[iter->first].by)
                    group_set[iter->first].by = j;
                average_order[iter->first].val += max_layer;
                ++average_order[iter->first].times;
            }
        }
    for (i = 0; i < max; ++i)
        if (pre_solve[i] == true)
            group_set[i].pi = -1;
    for (i = 1; i < max_xx; ++i)
        for (j = 0; j < max_yy; ++j) {
            for (k = 0; k < max_zz && cur_map_3d[i][j][k].edge_list[LEFT]->max_cap == 0; ++k) {
            }
            Edge_2d& edgeWest = construct_2d_tree.congestionMap2d.edge(i, j, DIR_WEST);
            if ((int) (edgeWest.used_net.size() << 1) > cur_map_3d[i][j][k].edge_list[LEFT]->max_cap) {
                if (edgeWest.used_net.size() > 1) {
                    RoutedNetTable::iterator iter = edgeWest.used_net.begin();
                    a = backtrace(iter->first);
                    for (iter++; iter != edgeWest.used_net.end(); ++iter) {
                        b = backtrace(iter->first);
                        if (a != b) {
                            group_set[b].pi = a;
                            group_set[a].num += group_set[b].num;
                        }
                    }
                }
            }
        }
    for (i = 0; i < max_xx; ++i)
        for (j = 1; j < max_yy; ++j) {
            for (k = 0; k < max_zz && cur_map_3d[i][j][k].edge_list[BACK]->max_cap == 0; ++k) {
            }
            Edge_2d& edgeSouth = construct_2d_tree.congestionMap2d.edge(i, j, DIR_SOUTH);
            if ((int) (edgeSouth.used_net.size() << 1) > cur_map_3d[i][j][k].edge_list[BACK]->max_cap) {
                if (edgeSouth.used_net.size() > 1) {
                    RoutedNetTable::iterator iter = edgeSouth.used_net.begin();
                    a = backtrace(iter->first);
                    for (iter++; iter != edgeSouth.used_net.end(); ++iter) {
                        b = backtrace(iter->first);
                        if (a != b) {
                            group_set[b].pi = a;
                            group_set[a].num += group_set[b].num;
                        }
                    }
                }
            }
        }
    for (i = 0; i < max; ++i) {
        if (temp_buf[2] == '0' || temp_buf[2] == '1')	// normal and random

            average_order[i].average = (double) (construct_2d_tree.rr_map.get_netPinNumber(i)) / (average_order[i].times + average_order[i].bends);

        else if (temp_buf[2] == '2')	// length
            average_order[i].average = (1.0 / (average_order[i].times));
        else if (temp_buf[2] == '3')	// pinnum
            average_order[i].average = (double) (construct_2d_tree.rr_map.get_netPinNumber(i));
        else
            // avgdensity
            average_order[i].average = (double) (average_order[i].val) / (average_order[i].times);
        if (pre_solve[i] == true)
            pre_solve_counter++;
    }
}

void Layer_assignment::initial_BFS_color_map() {
    int i, j, k;

    for (i = 0; i < max_xx; ++i)
        for (j = 0; j < max_yy; ++j)
            for (k = 0; k < max_zz; ++k)
                BFS_color_map[i][j][k] = -1;
}

void Layer_assignment::malloc_BFS_color_map() {
    int i, j;

    BFS_color_map = std::vector<std::vector<std::vector<int>>>(max_xx);
    for (i = 0; i < max_xx; ++i) {
        BFS_color_map[i] = std::vector<std::vector<int>>(max_yy);
        for (j = 0; j < max_yy; ++j)
            BFS_color_map[i][j] = std::vector<int>(max_zz);
    }
}

void Layer_assignment::calculate_wirelength() {
    int i, j, k, xy = 0, z = 0;
    LRoutedNetTable::iterator iter;

    for (k = 0; k < max_zz; ++k) {
        for (i = 1; i < max_xx; ++i)
            for (j = 0; j < max_yy; ++j)
                if (cur_map_3d[i][j][k].edge_list[LEFT]->used_net.size()) {
                    xy += cur_map_3d[i][j][k].edge_list[LEFT]->used_net.size();
                }
        for (i = 0; i < max_xx; ++i)
            for (j = 1; j < max_yy; ++j)
                if (cur_map_3d[i][j][k].edge_list[BACK]->used_net.size()) {
                    xy += cur_map_3d[i][j][k].edge_list[BACK]->used_net.size();
                }
    }
    for (i = 0; i < max_xx; ++i)
        for (j = 0; j < max_yy; ++j)
            for (k = 1; k < max_zz; ++k)
                z += (construct_2d_tree.via_cost * cur_map_3d[i][j][k].edge_list[DOWN]->used_net.size());

    printf("total wirelength = %d + %d = %d\n", xy, z, xy + z);

}

void Layer_assignment::sort_net_order() {
    int i, max;
    LRoutedNetTable count_data;
    LRoutedNetTable::iterator iter;
    LRoutedNetTable two_pin_data;
//vector<Pin*>* pin_list;
    clock_t start, finish;

    max = construct_2d_tree.rr_map.get_netNumber();
// re-distribute net
    multi_pin_net = std::vector<MULTIPIN_NET_NODE>(max);
    find_group(max);
    initial_overflow_map();
    std::vector<int> temp_net_order(max);
    for (i = 0; i < max; ++i) {
        temp_net_order[i] = i;
    }
    std::sort(temp_net_order.begin(), temp_net_order.end(), [&](int a,int b) {return comp_temp_net_order(a,b);});

    start = clock();

    for (i = 0; i < max; ++i) {
        klat(temp_net_order[i]);	// others
    }

    printf("cost = %d\n", global_pin_cost);
    finish = clock();
    printf("time = %lf\n", (double) (finish - start) / CLOCKS_PER_SEC);

}

void Layer_assignment::calculate_cap() {

    int overflow = 0;
    int max = 0;
    for (Edge_2d& edge : congestion.congestionMap2d.all()) {
        if (edge.isOverflow()) {
            overflow += (edge.overUsage() * 2);
            if (max < edge.overUsage())
                max = edge.overUsage() * 2;
        }
    }
    printf("2D overflow = %d\n", overflow);
    printf("2D max overflow = %d\n", max);
}

void Layer_assignment::generate_all_output() {
    int i, max = construct_2d_tree.rr_map.get_netNumber();

    for (i = 0; i < max; ++i)
        generate_output(i);
}

Layer_assignment::Layer_assignment(const std::string& outputFileNamePtr, Construct_2d_tree& construct_2d_tree,	//
        Congestion& congestion) :
        construct_2d_tree { construct_2d_tree }, cur_map_3d { construct_2d_tree.cur_map_3d },	//
        congestion { congestion } {

    string outputFileName { outputFileNamePtr };

    construct_2d_tree.via_cost = 1;

    max_xx = construct_2d_tree.rr_map.get_gridx();
    max_yy = construct_2d_tree.rr_map.get_gridy();
    max_zz = construct_2d_tree.rr_map.get_layerNumber();
    malloc_space();

    calculate_cap();
    initial_3D_coordinate_map();
    congestion.find_overflow_max(max_zz);
    puts("Layer assignment processing...");

    sort_net_order();
    print_max_overflow();

    puts("Layer assignment complete.");

    calculate_wirelength();

    printf("Outputing result file to %s\n", outputFileName.c_str());
    malloc_BFS_color_map();
    initial_BFS_color_map();

    int stdout_fd = dup(1);
    FILE* outputFile = freopen(outputFileName.c_str(), "w", stdout);
    generate_all_output();
    fclose(outputFile);

    stdout = fdopen(stdout_fd, "w");

}
void Layer_assignment::init_3d_map() {

    /*allocate space for cur_map_3d*/

    int x = rr_map.get_gridx();
    int y = rr_map.get_gridy();
    int z = rr_map.get_layerNumber();
    cur_map_3d.resize(x, y, z);

}

