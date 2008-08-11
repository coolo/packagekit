#!/usr/bin/python
#
# (c) 2008 Canonical Ltd.
# (c) 2008 Aidan Skinner <aidan@skinner.me.uk>
# Author: Martin Pitt <martin.pitt@ubuntu.com>
# License: LGPL 2.1 or later
#
# Synchronous PackageKit client wrapper for Python.

import gobject
import dbus

class PackageKitError(Exception):
    '''PackageKit error.

    This class mainly wraps a PackageKit "error enum". See
    http://www.packagekit.org/pk-reference.html#introduction-errors for details
    and possible values.
    '''
    def __init__(self, error):
        self.error = error

    def __str__(self):
        return self.error

class PackageKitClient:
    '''PackageKit client wrapper class.

    This exclusively uses synchonous calls. Functions which take a long time
    (install/remove packages) have callbacks for progress feedback.
    '''
    def __init__(self, main_loop=None):
        '''Initialize a PackageKit client.

        If main_loop is None, this sets up its own gobject.MainLoop(),
        otherwise it attaches to the specified one.
        '''
        self.pk_control = None
        if main_loop is None:
            import dbus.mainloop.glib
            main_loop = gobject.MainLoop()
            dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
        self.main_loop = main_loop

        self.bus = dbus.SystemBus()

    def _wrapCall(self, pk_xn, method, callbacks):
        '''
        Wraps a call which emits Finished and ErrorCode on completion
        '''
        pk_xn.connect_to_signal('Finished', self._h_finished)
        pk_xn.connect_to_signal('ErrorCode', self._h_error)
        for cb in callbacks.keys():
            pk_xn.connect_to_signal(cb, callbacks[cb])

        polkit_auth_wrapper(method)
        self._wait()
        if self._error_enum:
            raise PackageKitError(self._error_enum)


    def _wrapBasicCall(self, pk_xn, method):
        return self._wrapCall(pk_xn, method, {})

    def _wrapPackageCall(self, pk_xn, method):
        '''
        Wraps a call which emits Finished, ErrorCode on completion and
        Package for information returns a list of dicts with
        'installed', 'id' and 'summary' keys
        '''

        result = []
        package_cb = lambda i, id, summary: result.append(
            {'installed' : (i == 'installed'),
             'id': (str(id)),
             'summary' : str(summary)
             })
        self._wrapCall(pk_xn, method, {'Package' : package_cb})
        return result

    def _wrapDetailsCall(self, pk_xn, method):
        '''
        Wraps a call which emits Finished, ErrorCode on completion and
        Details for information returns a list of dicts with 'id',
        'license', 'group', 'description', 'upstream_url', 'size'.keys
        '''
        result = []
        details_cb = lambda id, license, group, detail, url, size: result.append
        ({"id" : str(id),
          "license" : str(license),
          "group" : str(group),
          "detail" : str(detail),
          "url" : str(url),
          "size" : int(size)
          })

        self._wrapCall(pk_xn, method, {'Details' : details_cb})
        return result

    def _wrapReposCall(self, pk_xn, method):
        '''
        Wraps a call which emits Finished, ErrorCode and RepoDetail
        for information returns a list of dicts with 'id',
        'description', 'enabled' keys
        '''
        result = []
        repo_cb = lambda id, description, enabled: result.append
        ({'id' : str(id),
          'desc' : str(description),
          'enabled' : enabled})
        self._wrapCall(pk_xn, method, {'RepoDetail' : repo_cb})
        return result


    def SuggestDaemonQuit(self):
        '''Ask the PackageKit daemon to shutdown.'''

        try:
            self.pk_control.SuggestDaemonQuit()
        except (AttributeError, dbus.DBusException), e:
            # not initialized, or daemon timed out
            pass

    def Resolve(self, filter, package):
        '''
        Resolve a package name to a PackageKit package_id filter and
        package are directly passed to the PackageKit transaction
        D-BUS method Resolve()

        Return Dict with keys of (installed, id, short_description)
        for all matches, where installed is a boolean and id and
        short_description are strings.
        '''
        xn = self._get_xn()
        return self._wrapPackageCall(xn, lambda : xn.Resolve(filter, package))


    def GetDetails(self, package_id):
        '''
        Get details about a PackageKit package_id.

        Return dict with keys (id, license, group, description,
        upstream_url, size).
        '''
        xn = self._get_xn()
        return self._wrapDetailsCall(xn, lambda : xn.GetDetails(package_id))

    def SearchName(self, filter, name):
        '''
        Search a package by name.
        '''
        xn = self._get_xn()
        return self._wrapPackageCall(xn, lambda : xn.SearchName(filter, name))

    def SearchDetails(self, filter, name):
        '''
        Search a packages details.
        '''
        xn = self._get_xn()
        return self._wrapPackageCall(xn,
                                     lambda : xn.SearchDetails(filter, name))


    def InstallPackages(self, package_ids, progress_cb=None):
        '''Install a list of package IDs.

        progress_cb is a function taking arguments (status, percentage,
        subpercentage, elapsed, remaining, allow_cancel). If it returns False,
        the action is cancelled (if allow_cancel == True), otherwise it
        continues.

        On failure this throws a PackageKitError or a DBusException.
        '''
        self._InstRemovePackages(package_ids, progress_cb, True, None, None)

    def RemovePackages(self, package_ids, progress_cb=None, allow_deps=False,
        auto_remove=True):
        '''Remove a list of package IDs.

        progress_cb is a function taking arguments (status, percentage,
        subpercentage, elapsed, remaining, allow_cancel). If it returns False,
        the action is cancelled (if allow_cancel == True), otherwise it
        continues.

        allow_deps and auto_remove are passed to the PackageKit function.

        On failure this throws a PackageKitError or a DBusException.
        '''
        self._InstRemovePackages(package_ids, progress_cb, False, allow_deps,
            auto_remove)

    def RefreshCache(self, force=False):
        '''
        Refresh the cache, i.e. download new metadata from a
        remote URL so that package lists are up to date. This action
        may take a few minutes and should be done when the session and
        system are idle.
        '''
        xn = self._get_xn()
        self._wrapBasicCall(xn, lambda : xn.RefreshCache(force))


    def GetRepoList(self, filter=None):
        '''
        Returns the list of repositories used in the system

        filter is a correct filter, e.g. None or 'installed;~devel'

        '''
        if (filter == None):
            filter = 'none'
        xn = self._get_xn()
        return self._wrapReposCall(xn, lambda : xn.GetRepoList(filter))


    def RepoEnable(self, repo_id, enabled):
        '''
        Enables the repository specified.

        repo_id is a repository identifier, e.g. fedora-development-debuginfo

        enabled true if enabled, false if disabled

        '''
        xn = self._get_xn()
        self._wrapBasicCall(xn, lambda : xn.RepoEnable(repo_id, enabled))

    def GetUpdates(self, filter=None):
        '''
        This method should return a list of packages that are installed and
        are upgradable.

        It should only return the newest update for each installed package.
        '''
        xn = self._get_xn()
        if (filter == None):
            filter = 'none'
        return self._wrapPackageCall(xn, lambda : xn.GetUpdates(filter))

    def UpdateSystem(self):
        '''
        This method should return a list of packages that are
        installed and are upgradable.

        It should only return the newest update for each installed package.
        '''
        xn = self._get_xn()
        self._wrapPackageCall(xn, lambda : xn.UpdateSystem())


    #
    # Internal helper functions
    #

    def _wait(self):
        '''Wait until an async PK operation finishes.'''
        self.main_loop.run()

    def _h_status(self, status):
        self._status = status

    def _h_allowcancel(self, allow):
        self._allow_cancel = allow

    def _h_error(self, enum, desc):
        self._error_enum = enum

    def _h_finished(self, status, code):
        self.main_loop.quit()

    def _h_progress(self, per, subper, el, rem):
        def _cancel(xn):
            try:
                xn.Cancel()
            except dbus.DBusException, e:
                if e._dbus_error_name == 'org.freedesktop.PackageKit.Transaction.CannotCancel':
                    pass
                else:
                    raise

        ret = self._progress_cb(self._status, int(per),
            int(subper), int(el), int(rem), self._allow_cancel)
        if not ret:
            # we get backend timeout exceptions more likely when we call this
            # directly, so delay it a bit
            gobject.timeout_add(10, _cancel, pk_xn)

    def _auth(self):
        policykit = self.bus.get_object(
            'org.freedesktop.PolicyKit.AuthenticationAgent', '/',
            'org.freedesktop.PolicyKit.AuthenticationAgent')
        if(policykit == None):
           print("Error: Could not get PolicyKit D-Bus Interface\n")
        granted = policykit.ObtainAuthorization("org.freedesktop.packagekit.update-system",
                                                (dbus.UInt32)(xid),
                                                (dbus.UInt32)(os.getpid()))

    def _InstRemovePackages(self, package_ids, progress_cb, install,
        allow_deps, auto_remove):
        '''Shared implementation of InstallPackages and RemovePackages.'''

        self._status = None
        self._allow_cancel = False

        pk_xn = self._get_xn()
        if progress_cb:
            pk_xn.connect_to_signal('StatusChanged', self._h_status)
            pk_xn.connect_to_signal('AllowCancel', self._h_allowcancel)
            pk_xn.connect_to_signal('ProgressChanged', self._h_progress)
            self._progress_cb = progress_cb
        pk_xn.connect_to_signal('ErrorCode', self._h_error)
        pk_xn.connect_to_signal('Finished', self._h_finished)
        if install:
            pk_xn.InstallPackages(package_ids)
        else:
            pk_xn.RemovePackages(package_ids, allow_deps, auto_remove)
        self._wait()
        if self._error_enum:
            raise PackageKitError(self._error_enum)
        if self._finished_status != 'success':
            raise PackageKitError('internal-error')

    def _get_xn(self):
        '''Create a new PackageKit Transaction object.'''

        self._error_enum = None
        self._finished_status = None
        try:
            tid = self.pk_control.GetTid()
        except (AttributeError, dbus.DBusException), e:
            if self.pk_control == None or (hasattr(e, '_dbus_error_name') and \
                e._dbus_error_name == 'org.freedesktop.DBus.Error.ServiceUnknown'):
                # first initialization (lazy) or timeout
                self.pk_control = dbus.Interface(self.bus.get_object(
                        'org.freedesktop.PackageKit',
                        '/org/freedesktop/PackageKit',
                    False), 'org.freedesktop.PackageKit')
                tid = self.pk_control.GetTid()
            else:
                raise

        return dbus.Interface(self.bus.get_object('org.freedesktop.PackageKit',
            tid, False), 'org.freedesktop.PackageKit.Transaction')

#### PolicyKit authentication borrowed wrapper ##
class PermissionDeniedByPolicy(dbus.DBusException):
    _dbus_error_name = 'org.freedesktop.PackageKit.PermissionDeniedByPolicy'


def polkit_auth_wrapper(fn, *args, **kwargs):
    '''Function call wrapper for PolicyKit authentication.

    Call fn(*args, **kwargs). If it fails with a PermissionDeniedByPolicy
    and the caller can authenticate to get the missing privilege, the PolicyKit
    authentication agent is called, and the function call is attempted again.
    '''
    try:
        return fn(*args, **kwargs)
    except dbus.DBusException, e:
        if e._dbus_error_name == PermissionDeniedByPolicy._dbus_error_name:
            # last words in message are privilege and auth result
            (priv, auth_result) = e.message.split()[-2:]
            if auth_result.startswith('auth_'):
                pk_auth = dbus.Interface(dbus.SessionBus().get_object(
                    'org.freedesktop.PolicyKit.AuthenticationAgent', '/', False),
                    'org.freedesktop.PolicyKit.AuthenticationAgent')
                # TODO: provide xid
                res = pk_auth.ObtainAuthorization(priv, dbus.UInt32(0),
                    dbus.UInt32(os.getpid()), timeout=300)
                if res:
                    return fn(*args, **kwargs)
            raise PermissionDeniedByPolicy(priv + ' ' + auth_result)
        else:
            raise

#
# Test code
#

if __name__ == '__main__':
    import subprocess, sys

    pk = PackageKitClient()

    print '---- RefreshCache() -----'''
    print pk.RefreshCache()

    print '---- Resolve() -----'
    pmount = pk.Resolve('none', 'pmount')
    print pmount
    print pk.Resolve('none', 'quilt')
    print pk.Resolve('none', 'foobar')
    print pk.Resolve('installed', 'coreutils')
    print pk.Resolve('installed', 'pmount')

    print '---- GetUpdates() ----'
    print pk.GetUpdates('none')

    print '---- GetDetails() -----'
    print pk.GetDetails(pmount[0]['id'])

    print '---- SearchName() -----'
    print pk.SearchName('available', 'coreutils')
    print pk.SearchName('installed', 'coreutils')

    #sys.exit(0)

    def cb(status, pc, spc, el, rem, c):
        print 'install pkg: %s, %i%%, cancel allowed: %s' % (status, pc, str(c))
        return True
        #return pc < 12

    print '---- UpdateSystem() ----'
    print pk.UpdateSystem()

    print '---- InstallPackages() -----'
    pk.InstallPackages(['pmount;0.9.17-2;i386;Ubuntu', 'quilt;0.46-6;all;Ubuntu'], cb)

    subprocess.call(['dpkg', '-l', 'pmount', 'quilt'])

    print '---- RemovePackages() -----'
    pk.RemovePackages(['pmount;0.9.17-2;i386;Ubuntu', 'quilt;0.46-6;all;Ubuntu'], cb)

    subprocess.call(['dpkg', '-l', 'pmount', 'quilt'])

    pk.SuggestDaemonQuit()