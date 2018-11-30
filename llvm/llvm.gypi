{
    'includes': [
        '../build/common.gypi',
    ],
    'variables': {
        'sources': [
            'initialize-llvm.cc',
            'log.cpp',
            'compiler-state.cc',
            'intrinsic-repository.cc',
            'common-values.cc',
            'output.cc',
            'compile.cc',
            'stack-maps.cc',
        ],
        'llvmlog_level': 0,
    },
}
