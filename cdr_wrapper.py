import sys
import ctypes

class ByteSegmentsArray:
    """
    Array of int items with predefined length.
    Could be passed to any C function as void* (use AsCVoidPointer method)
    Will be deleted by python automatically

    EXAMPLE:
    buf = ByteSegmentsArray(100, ByteSegmentsArray.INT16)
    testlib.fill_buf.argtypes = [ctypes.c_int, int_p]
    testlib.fill_buf(50, buf.AsCVoidPointer())
    print buf.AsPythonList()
    """
    INT8 = 1
    INT16 = 2
    INT32 = 4
    INT64 = 8
    def __init__(self, length, size=INT8):
        self.data_type = None
        if size==self.INT8: self.data_type = ctypes.c_int8
        if size==self.INT16: self.data_type = ctypes.c_int16
        if size==self.INT32: self.data_type = ctypes.c_int32
        if size==self.INT64: self.data_type = ctypes.c_int64
        self.c_data = (self.data_type * length)()
        self.length = length

    def AsCVoidPointer(self):
        """
        Get pointer to C data, for passing as argument by pointer to cytpes functions
        """
        return ctypes.cast(self.c_data, ctypes.c_void_p)

    def AsPythonList(self, length=None, offset=0):
        """
        Convert C data to python list, could use length and offset parameters.
        """
        ### Performance: 2.5M items array converts during 1 second
        ret = []
        if length is None: length = self.length
        for i in range(0, length):
            val = int(self.c_data[i + offset])
            if val is not None: ret+=[val]
        return ret

    def __getitem__(self, item):
        """
        Just to address i-th item of the obj

        EXAMPLE:
        print buf[i]
        """
        if type(item) != int or item>= self.length: return None
        return int(self.c_data[item])

class CdrWrapper:
    """
    CDR Wrapper class ver0.2a, see use example below
    """
    def __init__(self, absolute_lib_path, opt_argv0=None):
        """
        Loads library
        absolute_lib_path - path to CDR library .so or .dll file
        opt_argv0 - optionally argv0 param, defaultly gets sys.arv[0]
        """
        if (not opt_argv0): opt_argv0 = sys.argv[0]
        self.library = ctypes.CDLL(absolute_lib_path)
        self.argv0 = opt_argv0
    
    def MakeCdrChanCallback(self, python_callable):
        """
        Returns cdr callback from python callable function.
        python_callable - function that takes handle(integer), value(double), private_params(object) and returns error code (integer, TODO: check "0 if OK")

        EXAMPLE:
            def test_cb_py(handle, val, params):
                print "Python Callback:", handle, val, params
                return 0

        INNER: wraps python function by ctypes descriptor for c++
        """
        CB_FUNC = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_int, ctypes.c_double, ctypes.c_void_p)
        ret = CB_FUNC(python_callable)
        return ret 

    def CdrRegisterSimpleChan(self, name, cdr_callback, pivate_params=None):
        """
        Registers cdr callback by specified name event(???), with optional private params
        """
        self.library.CdrRegisterSimpleChan.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_void_p, ctypes.c_void_p]
        ret = self.library.CdrRegisterSimpleChan(name, self.argv0, cdr_callback, pivate_params)
        if (ret < 0): raise Exception("Error while Registering Simple Channel Callback, errcode: %s" % ret)
        return ret

    def CdrSetSimpleChanVal(self, handle, val):
        """
        Sets Simple Channel Value by handle
        handle - int, id of the channel
        val - double, value to set
        """
        self.library.CdrSetSimpleChanVal.restype = ctypes.c_int
        self.library.CdrSetSimpleChanVal.argtypes = [ctypes.c_int, ctypes.c_double]
        ret = self.library.CdrSetSimpleChanVal(handle, val)
        if (ret != 0): raise Exception("Error while Setting Simple Channel Value, errcode: %s" % ret)
        return ret

    def CdrGetSimpleChanVal(self, handle):
        """
        Returns Simple Channel Value, gotten by handle
        handle - int, id of the channel
        """
        self.library.CdrSetSimpleChanVal.restype = ctypes.c_int
        self.library.CdrSetSimpleChanVal.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_double)]
        val = ctypes.c_double(0.0)
        ret = self.library.CdrGetSimpleChanVal(handle, ctypes.byref(val))
        if (ret != 0): raise Exception("Error while Getting Simple Channel Value, errcode: %s" % ret)
        return val.value

#############################################
    def MakeCdrBigcCallback(self, python_callable):
        """
        Returns cdr callback from python callable function.
        python_callable - function that takes handle(integer), private_params(object) and returns error code (integer, TODO: check "0 if OK")

        EXAMPLE:
            def test_cb_py(handle, params):
                print "Python Callback:", handle, params
                return 0

        INNER: wraps python function by ctypes descriptor for c++
        """
        CB_FUNC = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_int, ctypes.c_void_p)
        ret = CB_FUNC(python_callable)
        return ret 

    def CdrRegisterSimpleBigc(self, name, max_datasize, cdr_callback, pivate_params=None):
        """
        Registers cdr callback by specified name event(???), with optional private params
        """
        self.library.CdrRegisterSimpleBigc.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p]
        ret = self.library.CdrRegisterSimpleChan(name, self.argv0, max_datasize, cdr_callback, pivate_params)
        if (ret < 0): raise Exception("Error while Registering Simple BigChan Callback, errcode: %s" % ret)
        return ret

    def CdrSetSimpleBigcParam(self, handle, n, val):
        """
        Sets Simple Bigc Param by handle
        handle - int, id of the bigc
        n - int, # of parameter to set
        val - double, value to set
        """
        self.library.CdrSetSimpleBigcParam.restype = ctypes.c_int
        self.library.CdrSetSimpleBigcParam.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int]
        ret = self.library.CdrSetSimpleBigcParam(handle, n, val)
        if (ret != 0): raise Exception("Error while Setting Simple BigChan Param, errcode: %s" % ret)
        return ret

    def CdrGetSimpleBigcParam(self, handle, n):
        """
        Returns Simple Bigc Param, gotten by handle
        handle - int, id of the bigc
        n - int, # of parameter to get
        """
        self.library.CdrSetSimpleBigcParam.restype = ctypes.c_int
        self.library.CdrSetSimpleBigcParam.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.POINTER(ctypes.c_double)]
        val = ctypes.c_double(0.0)
        ret = self.library.CdrGetSimpleBigcParam(handle, n, ctypes.byref(val))
        if (ret != 0): raise Exception("Error while Getting Simple BigChan Param, errcode: %s" % ret)
        return val.value

#############################################
############### USE EXAMPLE #################
#############################################
if __name__ == "__main__":
    print "Example starts"
    
    # Special QT import, needs to make qt libs visible for Cdrlib
    import DLFCN
    old_dlopen_flags = sys.getdlopenflags( )
    sys.setdlopenflags( old_dlopen_flags | DLFCN.RTLD_GLOBAL )
    from PyQt4 import QtCore, QtGui
    sys.setdlopenflags( old_dlopen_flags )

    # QT init
    application = QtGui.QApplication(sys.argv)
    myapp = QtGui.QMainWindow(None)
    myapp.show()
    
    # Definig simple callback python callable
    def test_cb_py(handle, val, params):
        print "Python Callback:", handle, val, params
        return 0

    # CDR
    wrapper = CdrWrapper('/home/makeev/Projects/PyQt/SandBox/qt_cxv2_test/cdrlib.so', sys.argv[0])
    callback = wrapper.MakeCdrChanCallback(test_cb_py)
    wrapper.CdrRegisterSimpleChan("test", callback, None) # Not None third parameter needs to be tested
    # Needs to be tested:
    # wrapper.CdrGetSimpleChanVal(10)
    # wrapper.CdrSetSimpleChanVal(10, 12.1)

    print "Example goes to main loop"
    # QT main loop    
    sys.exit(application.exec_())    
