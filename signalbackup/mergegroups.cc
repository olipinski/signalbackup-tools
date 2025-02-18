/*
  Copyright (C) 2019-2023  Selwin van Dijk

  This file is part of signalbackup-tools.

  signalbackup-tools is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  signalbackup-tools is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with signalbackup-tools.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "signalbackup.ih"

void SignalBackup::mergeGroups(std::vector<std::string> const &groupids)
{
  if (groupids.size() < 2)
  {
    std::cout << "Too few addresses" << std::endl;
    return;
  }

  std::cout << "" << std::endl;
  std::cout << "THIS FUNCTION MAY NEED UPDATING. PLEASE OPEN AN ISSUE" << std::endl;
  std::cout << "IF YOU NEED IT." << std::endl;
  std::cout << "" << std::endl;

  std::string targetgroup = groupids.back();

  SqliteDB::QueryResults res;
  std::set<std::string> targetmembersvec;
  if (d_database.tableContainsColumn("groups", "members"))
  {
    d_database.exec("SELECT members FROM groups WHERE group_id = ?", targetgroup, &res);
    std::string targetmembers = res.getValueAs<std::string>(0, 0);
    std::stringstream ss(targetmembers);
    while (ss.good())
    {
      std::string substr;
      std::getline(ss, substr, ',');
      targetmembersvec.insert(substr);
    }
  }
  else
  {
    d_database.exec("SELECT DISTINCT recipient_id FROM group_membership WHERE group_id = ?", targetgroup, &res);
    for (uint i = 0; i < res.rows(); ++i)
      targetmembersvec.insert(res.valueAsString(i, 0));
  }


  // get the thread_id of the target
  long long int tid = getThreadIdFromRecipient(targetgroup);

  if (tid != -1)
  {

    // update all messages from this addresses[i] to belong to that same thread and change address in new number
    for (uint i = 0; i < groupids.size() - 1; ++i)
    {
      long long int oldtid = getThreadIdFromRecipient(groupids[i]);
      if (oldtid == -1)
      {
        std::cout << "Failed to find thread for old group: " << groupids[i] << std::endl;
        continue;
      }

      std::cout << "Dealing with group: " << groupids[i] << std::endl;
      if (d_database.containsTable("sms"))
      {
        d_database.exec("UPDATE sms SET thread_id = ? WHERE thread_id = ?", {tid, oldtid});
        std::cout << "Updated " << d_database.changed() << " entries in 'sms' table" << std::endl;
        d_database.exec("UPDATE sms SET " + d_sms_recipient_id + " = ? WHERE " + d_sms_recipient_id + " = ?",
                        {targetgroup, groupids[i]});
        std::cout << "Updated " << d_database.changed() << " entries in 'sms' table" << std::endl;
      }
      d_database.exec("UPDATE " + d_mms_table + " SET thread_id = ? WHERE thread_id = ?", {tid, oldtid});
      std::cout << "Updated " << d_database.changed() << " entries in 'mms' table" << std::endl;

      if (!d_database.tableContainsColumn(d_mms_table, "to_recipient_id")) // < dbv185
      {
        d_database.exec("UPDATE " + d_mms_table + " SET " + d_mms_recipient_id + " = ? WHERE " + d_mms_recipient_id + " = ?",
                        {targetgroup, groupids[i]});
        std::cout << "Updated " << d_database.changed() << " entries in 'mms' table" << std::endl;
      }
      else
      {
        // adjust to_recipient (= group id on outgoing messages)
        d_database.exec("UPDATE " + d_mms_table + " SET to_recipient_id = ? WHERE to_recipient_id = ?",
                        {targetgroup, groupids[i]});
        std::cout << "Updated " << d_database.changed() << " entries in 'mms' table" << std::endl;
      }

      if (d_database.containsTable("mention"))
      {
        d_database.exec("UPDATE mention SET thread_id = ? WHERE thread_id = ?", {tid, oldtid});
        std::cout << "Updated " << d_database.changed() << " entries in 'sms' table" << std::endl;
      }

      if (d_database.containsTable("msl_recipient"))
        d_database.exec("UPDATE msl_recipient SET recipient_id = ? WHERE recipient_id = ?", {targetgroup, groupids[i]});

      if (d_database.containsTable("reaction")) // dbv >= 121
        d_database.exec("UPDATE reaction SET author_id = ? WHERE author_id = ?", {targetgroup, groupids[i]});

      // delete old (now empty) thread
      d_database.exec("DELETE FROM thread WHERE " + d_thread_recipient_id + " = ?", groupids[i]);
      std::cout << "Removed " << d_database.changed() << " threads from table" << std::endl;

      // get members of groupids[i] and merge them into targetgroup
      if (d_database.tableContainsColumn("groups", "members"))
      {
        d_database.exec("SELECT members FROM groups WHERE group_id = ?", groupids[i], &res);
        std::string members = res.getValueAs<std::string>(0, 0);
        std::stringstream ss2(members);
        while (ss2.good())
        {
          std::string substr;
          std::getline(ss2, substr, ',');
          auto [it, inserted] = targetmembersvec.insert(substr);
          if (inserted)
            std::cout << "Added " << substr << " to memberlist of group" << std::endl;
          else
            std::cout << "Skipped adding " << substr << " to group: already a member" << std::endl;
        }
      }
      else
      {
        d_database.exec("SELECT DISTINCT recipient_id FROM group_membership WHERE group_id = ?", groupids[i], &res);
        for (uint g = 0; g < res.rows(); ++g)
          d_database.exec("INSERT OR IGNORE INTO group_membership (group_id, recipient_id) VALUES (?, ?)", {targetgroup, res.getValueAs<long long int>(g, "recipient_id")});
      }

      // delete the merged group
      d_database.exec("DELETE FROM groups WHERE group_id = ?", groupids[i]);
      std::cout << "Removed " << d_database.changed() << " groups from table" << std::endl;

      if (d_database.containsTable("group_membership"))
        d_database.exec("DELETE FROM group_membership WHERE group_id = ?", groupids[i]);
    }

    // set new member list
    if (d_database.tableContainsColumn("groups", "members"))
    {
      std::cout << "Setting new memberlist" << std::endl;
      std::string newmemberlist;
      for (auto const &it : targetmembersvec)
        newmemberlist += it + ',';
      newmemberlist.pop_back(); // remove trailing comma...
      d_database.exec("UPDATE groups SET members = ? WHERE group_id = ?", {newmemberlist, targetgroup});
    }
  }
  else
  {
    std::cout << "Warning: no group thread with id " << tid << " found" << std::endl;
  }
  updateThreadsEntries();
}
