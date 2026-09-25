#pragma once
#include <memory>
class GLArea;
class QPainter;

// Kept opaque so plugins using GLArea do not acquire an SDK dependency.
class OpenAxisController {
public:
    explicit OpenAxisController(GLArea &);
    ~OpenAxisController();
    void refresh();
    void paint(QPainter &);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
