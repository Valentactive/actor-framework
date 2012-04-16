/******************************************************************************\
 *           ___        __                                                    *
 *          /\_ \    __/\ \                                                   *
 *          \//\ \  /\_\ \ \____    ___   _____   _____      __               *
 *            \ \ \ \/\ \ \ '__`\  /'___\/\ '__`\/\ '__`\  /'__`\             *
 *             \_\ \_\ \ \ \ \L\ \/\ \__/\ \ \L\ \ \ \L\ \/\ \L\.\_           *
 *             /\____\\ \_\ \_,__/\ \____\\ \ ,__/\ \ ,__/\ \__/.\_\          *
 *             \/____/ \/_/\/___/  \/____/ \ \ \/  \ \ \/  \/__/\/_/          *
 *                                          \ \_\   \ \_\                     *
 *                                           \/_/    \/_/                     *
 *                                                                            *
 * Copyright (C) 2011, 2012                                                   *
 * Dominik Charousset <dominik.charousset@haw-hamburg.de>                     *
 *                                                                            *
 * This file is part of libcppa.                                              *
 * libcppa is free software: you can redistribute it and/or modify it under   *
 * the terms of the GNU Lesser General Public License as published by the     *
 * Free Software Foundation, either version 3 of the License                  *
 * or (at your option) any later version.                                     *
 *                                                                            *
 * libcppa is distributed in the hope that it will be useful,                 *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of             *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.                       *
 * See the GNU Lesser General Public License for more details.                *
 *                                                                            *
 * You should have received a copy of the GNU Lesser General Public License   *
 * along with libcppa. If not, see <http://www.gnu.org/licenses/>.            *
\******************************************************************************/


#include <iostream>
#include "cppa/to_string.hpp"

#include "cppa/self.hpp"
#include "cppa/detail/invokable.hpp"
#include "cppa/abstract_event_based_actor.hpp"

namespace cppa {

abstract_event_based_actor::abstract_event_based_actor()
    : super(abstract_event_based_actor::blocked)
{
    m_mailbox_pos = m_mailbox.cache().before_begin();
}

void abstract_event_based_actor::dequeue(behavior&)
{
    quit(exit_reason::unallowed_function_call);
}

void abstract_event_based_actor::dequeue(partial_function&)
{
    quit(exit_reason::unallowed_function_call);
}

bool abstract_event_based_actor::handle_message(queue_node& node)
{
    auto& bhvr = *(m_loop_stack.back());
    if (bhvr.timeout().valid())
    {
        switch (dq(node, bhvr.get_partial_function()))
        {
            case dq_timeout_occured:
            {
                bhvr.handle_timeout();
                // fall through
            }
            case dq_done:
            {
                // callback might have called become()/unbecome()
                // request next timeout if needed
                if (!m_loop_stack.empty())
                {
                    auto& next_bhvr = *(m_loop_stack.back());
                    request_timeout(next_bhvr.timeout());
                }
                return true;
            }
            default: return false;
        }
    }
    else
    {
        return dq(node, bhvr.get_partial_function()) == dq_done;
    }
}

void abstract_event_based_actor::resume(util::fiber*, resume_callback* callback)
{
    self.set(this);
    auto done_cb = [&]()
    {
        m_state.store(abstract_scheduled_actor::done);
        while (!m_loop_stack.empty()) m_loop_stack.pop_back();
        on_exit();
        callback->exec_done();
    };
    if (m_loop_stack.empty())
    {
        cleanup(exit_reason::normal);
        done_cb();
        return;
    }
    auto mbox_end = m_mailbox.cache().end();
    auto rm_fun = [this](queue_node& node) { return handle_message(node); };
    for (;;)
    {
        try
        {
            auto iter = m_mailbox.cache().remove_first(rm_fun, m_mailbox_pos);
            if (iter == mbox_end)
            {
                // try fetch more
                m_mailbox_pos = m_mailbox.cache().before_end();
                if (m_mailbox.can_fetch_more() == false)
                {
                    m_state.store(abstract_scheduled_actor::about_to_block);
                    CPPA_MEMORY_BARRIER();
                    if (   m_mailbox.can_fetch_more()
                        || compare_exchange_state(abstract_scheduled_actor::about_to_block,
                                                  abstract_scheduled_actor::blocked        ) != abstract_scheduled_actor::blocked)
                    {
                        // someone preempt us
                        m_mailbox.try_fetch_more();
                    }
                    else
                    {
                        // try again next time
                        return;
                    }
                }
                else
                {
                    m_mailbox.try_fetch_more();
                }
            }
            else
            {
                // prepare for next receive
                m_mailbox_pos = m_mailbox.cache().before_begin();
            }
        }
        catch (actor_exited& what)
        {
            cleanup(what.reason());
            done_cb();
            return;
        }
        catch (...)
        {
            cleanup(exit_reason::unhandled_exception);
            done_cb();
            return;
        }
        if (m_loop_stack.empty())
        {
            cleanup(exit_reason::normal);
            done_cb();
            return;
        }
    }
}

void abstract_event_based_actor::on_exit()
{
}

} // namespace cppa
