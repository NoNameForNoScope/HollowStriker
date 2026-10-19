#ifndef BODYCAM_FSM_HPP
#define BODYCAM_FSM_HPP

namespace bodycam {
    enum class Faction { Blue, Red };
    enum class FsmState { Move, Investigate, Pursue, Engage, Reload, Dead };
    enum class Policy { Hold, Patrol, AdvanceToObjective, AttackMove };
}

#endif
