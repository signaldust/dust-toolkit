
#pragma once

#include "defs.h"
#include "hash.h"

#include <memory>

namespace dust
{
    struct ComponentHost;

    struct ComponentSystem
    {
        // ComponentManagerBase is a base-class for component managers
        // The only class that should inherit this is ComponentManager<T>
        struct ComponentManagerBase
        {
            ComponentManagerBase()
            {
                registered = false;
            }
            virtual ~ComponentManagerBase()
            {
                unregisterManager();
            }

            virtual void destroyComponent(ComponentHost * host) = 0;
            virtual void destroyAll() = 0;

        protected:
            void registerManager()
            {
                if(registered) return;
                sys.registerManager(this);
                registered = true;
            }

            void unregisterManager()
            {
                if(!registered) return;
                sys.unregisterManager(this);
                registered = false;
            }

        private:
            bool    registered;
        };

        static void destroyComponents(ComponentHost * host)
        {
            // if we destroy nested managers inside host
            // then managers later might get moved earlier
            // so iterate backwards and deal with shrinking list
            for(unsigned m = sys.managers.size(); m--;)
            {
                // this can happen :)
                if(m >= sys.managers.size()) continue;

                // we might sometimes call a manager multiple
                // times, but that won't cause any problems
                sys.managers[m]->destroyComponent(host);
            }
        }

    private:
        ComponentSystem()
        {
        }
        ~ComponentSystem()
        {
            // while we have managers, tell them to destroy all
            // components, which will then cause them to unregister
            while(managers.size()) { managers.front()->destroyAll(); }
        }

        std::vector<ComponentManagerBase*> managers;

        static void registerManager(ComponentManagerBase * manager)
        {
            sys.managers.push_back(manager);
        }

        static void unregisterManager(ComponentManagerBase * manager)
        {
            for(int i = 0; i < sys.managers.size(); ++i)
            {
                if(sys.managers[i] == manager)
                {
                    sys.managers[i] = sys.managers.back();
                    sys.managers.pop_back();
                    return;
                }
            }
        }


        static ComponentSystem sys;
    };


    // host is a simple object with just a vtable
    struct ComponentHost
    {
        virtual ~ComponentHost() { destroyComponents(); }
        void destroyComponents()
        {
            ComponentSystem::destroyComponents(this);
        }
    };

    // ComponentManager creates at most one component
    // of the given type per unique host.
    //
    template<typename T, typename Host = ComponentHost>
    struct ComponentManager : ComponentSystem::ComponentManagerBase
    {
        ~ComponentManager()
        {
            destroyAll();
        }

        template<typename Fn> void foreach(Fn && fn) { components.foreach(fn); }

        void destroyAll()
        {
            components.foreach([](ComponentMap & cm) { cm.component.reset(); });
            components.clear();
            unregisterManager();
        }

        void destroyComponent(ComponentHost * host)
        {
            auto * cm = components.find(host);
            if(cm)
            {
                cm->component.reset();
                components.remove(host);
            }

            // if this was the last object, unregister
            if(!components.size()) unregisterManager();
        }

        // return a component for the host, create new if necessary
        T * getComponent(Host * host)
        {
            auto * cm = components.find(host);
            if(cm) return cm->component.get();

            
            if(!components.size()) registerManager();
            auto * cptr = new T();

            components.insert(ComponentMap{host, std::unique_ptr<T>(cptr)});
            return cptr;
        }

        T & getReference(Host * host) { return *getComponent(host); }

        // return a component pointer or nullptr if there is none
        // doesn't create new components
        T * queryComponent(Host * host)
        {
            auto * cm = components.find(host);
            return cm ? cm->component.get() : 0;
        }
    private:

        struct ComponentMap
        {
            Host                *key;
            std::unique_ptr<T>  component;

            ComponentHost const * getKey() const { return key; }
            bool keyEqual(ComponentHost const * host) const { return key == host; }

            static uint64_t getHash(ComponentHost const * host)
            {
                return hash64((uintptr_t)host);
            }
        };
        
        Table<ComponentMap>     components;
    };

};
