dnl
dnl forloop([var], [start], [end], [iterator])
dnl
divert(`-1')
define(`forloop', `pushdef(`$1', `$2')_forloop($@)popdef(`$1')')
define(`_forloop',
       `$4`'ifelse($1, `$3', `', `define(`$1', incr($1))$0($@)')')

dnl
dnl dump_thread([index])
dnl
define(`dump_thread',
`
          thread@$1 {
            #address-cells = <0x0>;
            #size-cells = <0x0>;
            compatible = "ibm,fake-thread";
            reg = <0x$1 0x0>;
            index = <0x$1>;
          };
')dnl

dnl
dnl dump_core_pre([index], [addr])
dnl
define(`dump_core_pre',
`
        core@$2 {
          #address-cells = <0x1>;
          #size-cells = <0x1>;
          compatible = "ibm,fake-core";
          reg = <0x$2 0x0>;
          index = <0x$1>;')

dnl
dnl dump_core_post()
dnl
define(`dump_core_post', `        };
')dnl

dnl
dnl dump_core([index], [addr], [num_threads])
dnl
define(`dump_core',
`dump_core_pre(`$1', `$2')
forloop(`i', `0', eval(`$3-1'), `dump_thread(i)')
dump_core_post()')

dnl
dnl dump_processor_pre([index], [addr])
dnl
define(`dump_processor_pre',
`
      pib@$2 {
        #address-cells = <0x1>;
        #size-cells = <0x1>;
        compatible = "ibm,fake-pib";
        reg = <0x$2 0x0>;
        index = <0x$1>;')

dnl
dnl dump_processor_post()
dnl
define(`dump_processor_post', `      };
')dnl

dnl
dnl dump_processor([index], [addr], [num_cores], [num_threads])
dnl
define(`dump_processor',dnl
`dump_processor_pre(`$1', `$2')
forloop(`i', `0', eval(`$3-1'), `dump_core(i, eval(10000+(i+1)*10), $4)')
dump_processor_post()')

dnl
dnl dump_fsi_pre([index], [addr])
dnl
define(`dump_fsi_pre',
`
    fsi@$2 {
      #address-cells = <0x1>;
      #size-cells = <0x1>;
      compatible = "ibm,fake-fsi";
      reg = <0x$2 0x0>;
      index = <0x$1>;')

dnl
dnl dump_fsi_post()
dnl
define(`dump_fsi_post', `    };')

dnl
dnl dump_fsi([index], [addr], [num_processors], [num_cores], [num_threads])
dnl
define(`dump_fsi',
`dump_fsi_pre(`$1', `$2')
forloop(`i', `0', eval(`$3-1'), `dump_processor(i, eval(10000+i*1000), $4, $5)')
dump_fsi_post()')
divert`'dnl

/dts-v1/;

/ {
    #address-cells = <0x1>;
    #size-cells = <0x1>;
dump_fsi(0, 0, 8, 4, 2)
};
