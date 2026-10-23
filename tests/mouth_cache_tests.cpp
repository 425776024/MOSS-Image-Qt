#include "../vendor/xinshen-ditto/liveportrait/mouth_cache_store.h"
#include <fstream>
#include <iostream>
using namespace ditto::mouth_cache;
void check(bool value) { if (!value) throw std::runtime_error("mouth cache assertion failed"); }
int main() {
    const auto root=fs::temp_directory_path()/("moss-mouth-cache-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if (!fs::create_directory(root)) return 1;
    try {
        const auto a=root/(std::string(64,'a')+".json"), b=root/(std::string(64,'b')+".json"), c=root/(std::string(64,'c')+".json");
        auto put=[](const fs::path &p, int size){std::ofstream(p)<<std::string(size,'x');};
        auto old=fs::file_time_type::clock::now()-std::chrono::hours(10);
        put(a,40);put(b,40);put(c,40);
        fs::last_write_time(a,old);fs::last_write_time(b,old+std::chrono::hours(1));fs::last_write_time(c,old+std::chrono::hours(2));
        {
            Lock lock(root);
            touch(a); // A cache hit must protect the oldest-created entry.
            check(prune(root,80)==80);check(fs::exists(a)&&!fs::exists(b)&&fs::exists(c));
            bool blocked=false;try {Lock competing(root);}catch(const std::exception &){blocked=true;}
            check(blocked);
        }
        // Existing oversized entries and files from an interrupted write are reclaimed.
        put(b,200);
        auto partial=root/(std::string(64,'d')+".json.123.456.partial");put(partial,17);
        auto feature=root/(std::string(64,'e')+"-native-silence150-v1.json");put(feature,20);
        put(root/"project.json",30);put(root/"video.mp4",30);
        fs::create_directories(root/"subdir");put(root/"subdir"/a.filename(),30);
        fs::create_symlink(root/"project.json",root/(std::string(64,'f')+".json"));
        {
            Lock lock(root);
            check(prune(root,80)<=80);check(!fs::exists(partial));check(!fs::exists(b));
            check(prune(root,80,true)==0);
            check(fs::exists(root/"project.json")&&fs::exists(root/"video.mp4"));
            check(fs::exists(root/"subdir"/a.filename()));
            check(fs::is_symlink(root/(std::string(64,'f')+".json")));
            check(prune(root,80,true)==0); // Empty clear is idempotent.
        }
        put(a,40);{Lock lock(root);check(prune(root,80)==40);} // Reusable after clearing.
        fs::remove_all(root);
        std::cout<<"cache capacity, LRU hit, shared features, locking, partial cleanup, clear scope: passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr<<e.what()<<'\n';fs::remove_all(root);return 1;
    }
}
