/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (c) 2007 Novell, Inc.
 * Copyright (c) 2007 Boyd Timothy <btimothy@gmail.com>
 * Copyright (c) 2007-2008 Stefan Haas <shaas@suse.de>
 * Copyright (c) 2007-2008 Scott Reeves <sreeves@novell.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef _ZYPP_UTILS_H_
#define _ZYPP_UTILS_H_

#undef ZYPP_BASE_LOGGER_LOGGROUP
#define ZYPP_BASE_LOGGER_LOGGROUP "packagekit"

#include <stdlib.h>
#include <glib.h>
#include <zypp/RepoManager.h>
#include <zypp/media/MediaManager.h>
#include <zypp/Resolvable.h>
#include <zypp/ResPool.h>
#include <zypp/PoolQuery.h>
#include <zypp/ResFilters.h>
#include <packagekit-glib2/pk-enum.h>

#include <iterator>
#include <list>
#include <set>

// some typedefs and functions to shorten Zypp names
typedef zypp::ResPoolProxy ZyppPool;
//inline ZyppPool zyppPool() { return zypp::getZYpp()->poolProxy(); }
typedef zypp::ui::Selectable::Ptr ZyppSelectable;
typedef zypp::ui::Selectable*		ZyppSelectablePtr;
typedef zypp::ResObject::constPtr	ZyppObject;
typedef zypp::Package::constPtr		ZyppPackage;
//typedef zypp::Patch::constPtr		ZyppPatch;
//typedef zypp::Pattern::constPtr		ZyppPattern;
//inline ZyppPackage tryCastToZyppPkg (ZyppObject obj)
//	{ return zypp::dynamic_pointer_cast <const zypp::Package> (obj); }

typedef enum {
        INSTALL,
        REMOVE,
        UPDATE
} PerformType;

/** Used to show/install only an update to ourself. This way if we find a critical bug
  * in the way we update packages we will install the fix before any other updates.
  */
extern gboolean _updating_self;

/** A string to store the last refreshed repo
  * this is needed for gpg-key handling stuff (UGLY HACK)
  * FIXME
  */
extern gchar *_repoName;

class ZyppJob {
 public:
	ZyppJob(PkBackendJob *job);
	~ZyppJob();
	zypp::ZYpp::Ptr get_zypp();
};

/**
  * Enable and rotate logging
  */
gboolean zypp_logging ();

gboolean zypp_is_changeable_media (PkBackend *backend, const zypp::Url &url);

gboolean zypp_is_development_repo (PkBackend *backend, zypp::RepoInfo repo);

gboolean zypp_is_valid_repo (PkBackendJob *job, zypp::RepoInfo repo);
/**
 * Build and return a ResPool that contains all local resolvables
 * and ones found in the enabled repositories.
 */
zypp::ResPool zypp_build_pool (zypp::ZYpp::Ptr zypp, gboolean include_local);

/**
* check and warns the user that a repository may be outdated
*/
void warn_outdated_repos(PkBackendJob *job, const zypp::ResPool & pool);

/**
  * Return the rpmHeader of a package
  */
zypp::target::rpm::RpmHeader::constPtr zypp_get_rpmHeader (const std::string &name, zypp::Edition edition);

/**
  * Return the PkEnumGroup of the given PoolItem.
  */
PkGroupEnum get_enum_group (const std::string &group);

/**
 * Returns a list of packages that match the specified package_name.
 */
void zypp_get_packages_by_name (PkBackend *backend, const gchar *package_name,
				const zypp::ResKind kind, std::vector<zypp::sat::Solvable> &result,
				gboolean include_local = TRUE);

/**
 * Returns a list of packages that owns the specified file.
 */
void zypp_get_packages_by_file (zypp::ZYpp::Ptr zypp, const gchar *search_file, std::vector<zypp::sat::Solvable> &result);

/**
 * Returns the Resolvable for the specified package_id.
 */
zypp::sat::Solvable zypp_get_package_by_id (PkBackend *backend, const gchar *package_id);

/**
 * Build a package_id from the specified resolvable.  The returned
 * gchar * should be freed with g_free ().
 */
gchar * zypp_build_package_id_from_resolvable (const zypp::sat::Solvable &resolvable);

/**
  * Get the RepoInfo
  */
zypp::RepoInfo
zypp_get_Repository (PkBackend *backend, const gchar *alias);

/**
  * Ask the User if it is OK to import an GPG-Key for a repo
  */
gboolean zypp_signature_required (PkBackendJob *job, const zypp::PublicKey &key);

/**
  * Ask the User if it is OK to refresh the Repo while we don't know the key
  */
gboolean zypp_signature_required (PkBackendJob *job, const std::string &file);

/**
  * Ask the User if it is OK to refresh the Repo while we don't know the key, only its id which was never seen before
  */
gboolean zypp_signature_required (PkBackendJob *job, const std::string &file, const std::string &id);

/**
  * Find best (according to edition) uninstalled item with the same kind/name/arch as item.
  */
zypp::PoolItem zypp_find_arch_update_item (const zypp::ResPool & pool, zypp::PoolItem item);

/**
  * Return the best, most friendly selection of update patches and packages that
  * we can find. Also manages _updating_self to prioritise critical infrastructure
  * updates.
  */
void zypp_get_updates (PkBackendJob *job, zypp::ZYpp::Ptr zypp, std::set<zypp::PoolItem> &);

/**
  * Sets the restart flag of a patch
  */
void zypp_check_restart (PkRestartEnum *restart, zypp::Patch::constPtr patch);

/**
  * simulate, or perform changes in pool to the system
  */
gboolean zypp_perform_execution (PkBackendJob *job, zypp::ZYpp::Ptr zypp, PerformType type, gboolean force, PkBitfield transaction_flags);

/**
 * should we omit a solvable from a result because of filtering ?
 */
gboolean zypp_filter_solvable (PkBitfield filters, const zypp::sat::Solvable &item);

/**
 * apply filters to a list.
 */
void     zypp_emit_filtered_packages_in_list (PkBackendJob *job, PkBitfield _filters, const std::vector<zypp::sat::Solvable> &list);

/**
  * build array of package_id's seperated by blanks out of the capabilities of a solvable
  */
GPtrArray * zypp_build_package_id_capabilities (zypp::Capabilities caps, gboolean terminate = TRUE);

/**
  * refresh the enabled repositories
  */
gboolean zypp_refresh_cache (PkBackendJob *job, zypp::ZYpp::Ptr zypp, gboolean force);

/**
  * helper to simplify returning errors
  */
void zypp_backend_finished_error (PkBackendJob  *job, PkErrorEnum err_code,
				  const char *format, ...);

/**
  * helper to emit pk package signals for a backend for a zypp solvable
  */
void     zypp_backend_package (PkBackendJob *job, PkInfoEnum info,
			       const zypp::sat::Solvable &pkg,
			       const char *opt_summary);

/**
  * helper to emit pk package status signals based on a ResPool object
  */
gboolean zypp_backend_pool_item_notify (PkBackendJob  *job,
					const zypp::PoolItem &item,
					gboolean sanity_check = FALSE);

/**
  * helper to compare a version + architecture, with source arch mangling.
  */
gboolean zypp_ver_and_arch_equal (const zypp::sat::Solvable &pkg,
				   const char *name, const char *arch);

/**
 * helper to refresh a repo's metadata and cache, catching signature
 * exceptions in a safe way.
 */
gboolean zypp_refresh_meta_and_cache (zypp::RepoManager &manager, zypp::RepoInfo &repo, bool force = false);

#endif // _ZYPP_UTILS_H_

