// Implementation of the permission agent interface and its verdict cache.
// See judge.h.
#include "judge.h"

#include <map>
#include <mutex>

namespace atui {
namespace tools {

namespace {

PermissionJudge *g_judge = 0;

// Guards the cache below. Tool calls happen on the worker thread while the UI
// thread can clear the cache from a settings dialog, so it is not single-thread
// by accident.
std::mutex &cacheMutex()
{
    static std::mutex m;
    return m;
}

// Insertion order, so the cap can drop the oldest entries rather than a random
// half of the map.
struct Cache {
    std::map< std::string, EffectPlan > plans;
    std::vector< std::string > order;
};

Cache &cache()
{
    static Cache c;
    return c;
}

// Roughly one screen of commands per session is plenty; past it the oldest are
// dropped. A stale entry costs a round-trip, never safety, so this bound is
// allowed to be careless.
const std::size_t kMaxCachedPlans = 256;

} // anonymous namespace

void setPermissionJudge( PermissionJudge *judge )
{
    g_judge = judge;
}

PermissionJudge *permissionJudge()
{
    return g_judge;
}

const char *judgePromptVersion()
{
    // Bumped whenever the question asked of the agent changes (its wording or
    // its expected keys), so a build with a different prompt can never reuse a
    // plan cached under the old one.
    return "1";
}

std::string planCacheKey( const std::string &toolName,
                          const std::string &argumentsJson,
                          const std::string &model,
                          const std::string &root )
{
    // The parts that decide the answer, concatenated - deliberately not hashed.
    // A hash would make a collision a wrong permission decision, and the cache is
    // capped at a few hundred short commands, so there is nothing to win.
    return std::string( judgePromptVersion() ) + "\n" + model + "\n" + root +
           "\n" + toolName + "\n" + argumentsJson;
}

bool cachedPlan( const std::string &key, EffectPlan &out )
{
    std::lock_guard< std::mutex > lock( cacheMutex() );
    std::map< std::string, EffectPlan >::const_iterator it =
        cache().plans.find( key );
    if( it == cache().plans.end() ) return false;
    out = it->second;
    return true;
}

void rememberPlan( const std::string &key, const EffectPlan &plan )
{
    if( !plan.parsed ) return;   // nothing worth reusing, and nothing to fail on
    std::lock_guard< std::mutex > lock( cacheMutex() );
    Cache &c = cache();
    if( c.plans.count( key ) == 0 )
    {
        if( c.order.size() >= kMaxCachedPlans )
        {
            c.plans.erase( c.order.front() );
            c.order.erase( c.order.begin() );
        }
        c.order.push_back( key );
    }
    c.plans[ key ] = plan;
}

void clearPlanCache()
{
    std::lock_guard< std::mutex > lock( cacheMutex() );
    cache().plans.clear();
    cache().order.clear();
}

} // namespace atui::tools
} // namespace atui
