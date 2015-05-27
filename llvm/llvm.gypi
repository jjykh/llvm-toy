{
    'includes': [
        '../build/common.gypi',
    ],
    'variables': {
        'sources': [
            'InitializeLLVM.cpp',
            'LLVMAPI.cpp',
            'log.cpp',
            'CompilerState.cpp',
            'IntrinsicRepository.cpp',
            'CommonValues.cpp',
        ],
        'llvmlog_level': 0,
    },
}
