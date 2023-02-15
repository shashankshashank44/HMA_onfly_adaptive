#ifndef __REQUEST_H
#define __REQUEST_H

#include <vector>
#include <functional>

using namespace std;

namespace ramulator
{

class Request
{
public:
    bool is_first_command;
    long addr;
    // long addr_row;
    vector<int> addr_vec;
    // specify which core this request sent from, for virtual address translation
    int coreid;

	//Mz:
	long phys_addr;	//in default case ohys_addr = addr
	int req_target_mem;
    int req_category = 0;

    enum class Type
    {
        READ,
        WRITE,
        REFRESH,
        POWERDOWN,
        SELFREFRESH,
        EXTENSION,
        MAX
    } type;

    long arrive = -1;
    long depart;
    function<void(Request&)> callback; // call back with more info
    function<void(Request&)> mig_callback; // Mz onfly call back with more info

	//Mz: for default case introduced phys_addr = addr
    Request(long addr, Type type, int coreid = 0)
        : is_first_command(true), addr(addr), coreid(coreid), phys_addr(addr), req_target_mem(0), type(type), callback([](Request& req){}) {}

    Request(long addr, Type type, function<void(Request&)> callback, int coreid = 0)
        : is_first_command(true), addr(addr), coreid(coreid), phys_addr(addr), req_target_mem(0), type(type), callback(callback) {}

    Request(long addr, Type type, int req_category, function<void(Request&)> mig_callback, int coreid = 0)
        : is_first_command(true), addr(addr), coreid(coreid), phys_addr(addr), req_target_mem(0), req_category(req_category), type(type), mig_callback(mig_callback) {} // for onfly

    Request(vector<int>& addr_vec, Type type, function<void(Request&)> callback, int coreid = 0)
        : is_first_command(true), addr_vec(addr_vec), coreid(coreid),  req_target_mem(0), type(type), callback(callback) {}

    Request()
        : is_first_command(true), coreid(0), req_target_mem(0) {}
};

} /*namespace ramulator*/

#endif /*__REQUEST_H*/

