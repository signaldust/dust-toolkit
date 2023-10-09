
#pragma once

#include "button.h"
#include "scroll.h"

namespace dust
{
    // TabPanel manages the collection of tabs.
    //
    // Content must be default constructible and derived from Panel.
    // NoContent is set as content when there are no tabs.
    //
    // We template the type to track it for application code.
    //
    template <typename Content, typename NoContent>
    struct TabPanel : Panel
    {
        // this is really just the maximum size
        // we draw them smaller when we have too many
        static const unsigned tabSizePt = 90;

        Font font;

        struct Tab
        {
            Notify  onClose = doNothing;
            Notify  onSelect = doNothing;

            std::string label;
            Content content;

            bool    modified;   // draw a marker?
        };

        // placeholder when there are no tabs
        NoContent   noContent;

        ARGB        actColor = 0;

        Tab * newTab(const std::string & label, bool makeActive = true)
        {
            Tab * tab = new Tab();
            tab->label = label;

            tabs.push_back(std::unique_ptr<Tab>(tab));

            if(makeActive)
            {
                selectTab(tabs.size() - 1);
            }

            redraw();

            return tab;
        }

        // returns true if this panel manages this tab
        bool contains(Tab * tab)
        {
            for(auto & p : tabs)
            {
                if(tab == p.get()) return true;
            }
            return false;
        }

        void closeTab(Tab * tab)
        {
            for(auto i = tabs.begin(); i != tabs.end(); ++i)
            {
                if(i->get() == tab)
                {
                    tabs.erase(i);
                    break;
                }
            }

            redraw();

            // if we have no tabs, we're done
            if(!tabs.size())
            {
                noContent.setParent(contentView);
                return;
            }

            // otherwise activate next or last
            if(activeTab >= tabs.size())
            {
                activeTab = tabs.size() - 1;
            }
            selectTab(activeTab);
        }

        // returns pointer to active tab, or 0 if none
        Tab * getActiveTab()
        {
            return tabs.size() ? tabs[activeTab].get() : 0;
        }

        // returns pointer to tab by index, or 0 if out of bounds
        Tab * getTabByIndex(unsigned n)
        {
            if(n >= tabs.size()) return 0;
            return tabs[n].get();
        }

        void selectTab(unsigned n)
        {
            // don't crash if we try to pick a tab past the last
            if(!tabs.size()) return;
            if(n >= tabs.size()) n = tabs.size() - 1;
            
            activeTab = n;
            contentView.removeAllChildren();
            tabs[activeTab]->content.setParent(contentView);
            tabs[activeTab]->onSelect();
        }

        // select next tab with wrap-around
        void selectNextTab()
        {
            unsigned n = activeTab + 1;
            if(n == tabs.size()) n = 0;
            selectTab(n);
        }

        // select previous tab with wrap-around
        void selectPrevTab()
        {
            unsigned n = activeTab;
            if(!n) n = tabs.size();
            selectTab(n - 1);
        }

        // move the active tab to the left
        void moveTabLeft()
        {
            if(!tabs.size()) return;
            
            unsigned n = activeTab;
            if(n)
            {
                std::swap(tabs[n], tabs[n-1]);
                activeTab = n-1;
                redraw();
            }
        }

        // move the active tab to the right
        void moveTabRight()
        {
            if(!tabs.size()) return;

            unsigned n = activeTab;
            if(n+1 < tabs.size())
            {
                std::swap(tabs[n], tabs[n+1]);
                activeTab = n+1;
                redraw();
            }
            
        }

        // move the active tab to another panel
        void moveTabToPanel(TabPanel * to)
        {
            to->tabs.push_back(std::unique_ptr<Tab>());
            std::swap(to->tabs.back(), tabs[activeTab]);
            closeTab(0);
            to->selectTab(to->tabs.size() - 1);
        }

        TabPanel()
        {
            style.rule = LayoutStyle::FILL;

            strip.setParent(this);
            strip.panel = this;
            strip.style.rule = LayoutStyle::NORTH;

            contentView.setParent(this);
            contentView.style.rule = LayoutStyle::FILL;
            // matches the tab offset on left
            contentView.style.padding.west = 1;
            // don't want changing tabs changing rest of layout
            contentView.style.canScrollX = true;
            contentView.style.canScrollY = true;

            font.loadDefaultFont(7.f, 96.f);
            
            noContent.setParent(contentView);
        }

        void redrawStrip()
        {
            strip.redraw();
        }

        void ev_dpi(float dpi)
        {
            font.setDPI(dpi);
        }

        // Always do child layout locally
        void reflowChildren()
        {
            layoutAsRoot(getWindow()->getDPI());
            redraw();
        }
        
        // linking multiple panels into a circular list
        // allows tabs to be dragged between the panels
        TabPanel * dragLink = this;

    private:

        struct TabStrip : Panel
        {
            TabPanel    *panel;

            void render(RenderContext & rc)
            {
                float pt = getWindow()->pt();

                int m = (int) ceilf(pt);

                int tx = 0;

                unsigned nTabs = panel->tabs.size();
                if(!nTabs) return;
                int tw = std::min((layout.w + 2*m) / nTabs,
                    (unsigned) ceilf(pt * panel->tabSizePt));
                for(int i = 0; i < nTabs; ++i)
                {
                    ARGB bgColor = theme.bgColor;
                    ARGB fgColor = theme.fgColor;
                    int bm = 0;
                    if(panel->activeTab != i)
                    {
                        fgColor = theme.fgMidColor;
                        bgColor = theme.bgMidColor;
                        bm = m;
                    }

                    rc.fillRect(paint::Color(bgColor),
                        tx + m, m, tw - m, layout.h - m - bm);

                    if(panel->activeTab == i)
                    {
                        rc.fillRect(paint::Color(panel->actColor),
                           tx + m, 0, tw - m, m);
                    }

                    Font & f = panel->font;

                    float tOff = f->getLineHeight();

                    Rect rText(tx, 0, (int) (tw - tOff), layout.h);
                    RenderContext rcText(rc, rText);

                    if(panel->tabs[i]->modified)
                    {
                        tOff += rcText.drawText(f, "* ", -1, paint::Color(fgColor),
                            tx + tOff, f->getVertOffset() + .5f * layout.h);
                    }

                    rcText.drawText(f, panel->tabs[i]->label,
                        paint::Color(fgColor),
                        tx + tOff, f->getVertOffset() + .5f * layout.h);

                    tx += tw;
                }
            }

            int ev_size_y(float dpi)
            {
                if(!panel->font.valid(dpi)) return 0;

                return (int) ceilf(panel->font->getLineHeight() * 2);
            }

            bool ev_mouse(const MouseEvent & e)
            {
                unsigned nTabs = panel->tabs.size();
                if(!nTabs) return false;

                float pt = getWindow()->pt();
                int tw = std::min((layout.w + 2*(int) ceilf(pt)) / nTabs,
                    (unsigned) ceilf(pt * panel->tabSizePt));
                int tab = e.x / tw;

                if(e.type == MouseEvent::tDown)
                {
                    if(tab >= panel->tabs.size()) return false;

                    if(e.button == 1)
                    {
                        // we set last-active to the tab that was previously
                        // active as-if the newly selected one was dragged out
                        // this way we dragging an inactive tab to another panel
                        // restores the originally active tab
                        panel->lastActive = panel->activeTab;
                        if(tab < panel->activeTab) --panel->lastActive;
                        
                        panel->selectTab(tab);
                    }

                    if(e.button == 2)
                    {
                        panel->tabs[tab]->onClose();
                    }
                }

                if(e.type == MouseEvent::tMove)
                {
                    if(e.button == 1)
                    {
                        // check for hand-off to another panel first
                        int winX = e.x + layout.windowOffsetX;
                        int winY = e.y + layout.windowOffsetY;
                        
                        TabPanel * next = panel->dragLink;
                        while(next != panel)
                        {
                            int nX = next->layout.windowOffsetX;
                            int nY = next->layout.windowOffsetY;
                            
                            if(winX < nX || winX >= nX + next->layout.w
                            || winY < nY || winY >= nY + next->layout.h)
                            {
                                next = next->dragLink;
                                continue;
                            }
                            
                            // mouse is over other panel, do a hand-off
                            // we push a null-tab to the other, do a swap
                            // then close the null-tab from current panel
                            next->tabs.push_back(std::unique_ptr<Tab>());
                            next->lastActive = next->activeTab;
                            std::swap(next->tabs.back(),
                                panel->tabs[panel->activeTab]);
                            panel->closeTab(0);
                            // this is not necessarily valid anymore
                            // but selectTab will clip as necessary
                            panel->selectTab(panel->lastActive);
                            
                            // finally select the newly added tab and hand-off
                            next->selectTab(next->tabs.size() - 1);
                            getWindow()->redirectDrag(&next->strip);
                            
                            // also synthesize a new mouse event, so we don't
                            // wait until next move to fix tab order
                            MouseEvent es = e;
                            es.x += layout.windowOffsetX - next->layout.windowOffsetX;
                            es.y += layout.windowOffsetY - next->layout.windowOffsetY;
                            
                            next->ev_mouse(es);
                            
                            return true;                            
                        }
                    
                        if(tab < 0) tab = 0;
                        if(tab >= panel->tabs.size())
                        {
                            tab = panel->tabs.size() - 1;
                        }

                        unsigned & activeTab = panel->activeTab;

                        while(activeTab < tab)
                        {
                            std::swap(
                                panel->tabs[activeTab],
                                panel->tabs[activeTab+1]);
                            ++activeTab;
                        }

                        while(activeTab > tab)
                        {
                            std::swap(
                                panel->tabs[activeTab],
                                panel->tabs[activeTab-1]);
                            --activeTab;
                        }

                        redraw();
                    }
                }

                // we can eat all mouse events here
                return true;
            }
        };
        
        TabStrip    strip;
        Panel       contentView;

        std::vector< std::unique_ptr<Tab> > tabs;

        unsigned    activeTab;
        unsigned    lastActive; // for drag, see TabStrip mouse down
    };
};
