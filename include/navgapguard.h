#ifndef OPL_NAV_GAP_GUARD_H
#define OPL_NAV_GAP_GUARD_H

// Experimental navigation-only filter. Unknown reads can retain edge identity,
// but may neither emit movement nor spend the repeat timer. A verified release
// or disconnect is immediate. The retention expires after 100 ms of clock().
class NavigationGapGuard
{
public:
    static constexpr unsigned int NAV_MASK = 0x0cf0; // directions and L1/R1
    static constexpr unsigned int MAX_GAP_US = 100000;

    void begin(bool enabled, unsigned int nowUs)
    {
        enabled_ = enabled;
        nowUs_ = nowUs;
        unknown_ = known_ = 0;
    }

    unsigned int sample(unsigned int index, bool connected, bool valid, unsigned int buttons)
    {
        if (index >= 4)
            return valid ? buttons : 0;
        auto &s = slots_[index];
        if (valid) {
            s.nav = buttons & NAV_MASK;
            s.when = nowUs_;
            s.have = true;
            known_ |= s.nav;
            return buttons;
        }
        if (!connected) {
            s = {};
            return 0;
        }
        if (enabled_ && s.have && s.nav && (unsigned int)(nowUs_ - s.when) <= MAX_GAP_US) {
            unknown_ |= s.nav;
            return s.nav;
        }
        return 0;
    }

    unsigned int unknown() const { return unknown_ & ~known_; }

private:
    struct Slot
    {
        unsigned int nav = 0, when = 0;
        bool have = false;
    } slots_[4];
    unsigned int nowUs_ = 0, unknown_ = 0, known_ = 0;
    bool enabled_ = false;
};

#endif
