#!/usr/bin/env python3
"""F10 BIOS setup: real key navigation, persistence, defaults and failure handling."""
import struct
import zlib
import time
from pathlib import Path
import kotl_efi as efi
from kotl_ui import ui, multi_esp

OUT=efi.OUT


def navigate(sequence, screenshot=None, failed_save=False, final_enter_exits=False):
    def actions(key,read_until,command,data,proc):
        for index,name in enumerate(sequence):
            if name.startswith('pause:'):
                time.sleep(float(name.split(':')[1]));continue
            count=data.count(b'firmware-setup> ')
            key(name)
            if name in ('f10','esc') or (final_enter_exits and name=='ret' and index+1==len(sequence)):
                if name=='f10' and failed_save:
                    read_until(b'Firmware settings save failed')
                continue
            # Each navigation/edit redraw emits this marker. Drain new output,
            # rather than sleeping through a potentially slow framebuffer update.
            import os, select
            deadline=time.monotonic()+5
            while data.count(b'firmware-setup> ')<=count:
                if time.monotonic()>deadline:raise TimeoutError(name)
                if select.select([proc.stdout],[],[],.1)[0]:data.extend(os.read(proc.stdout.fileno(),65536))
        if screenshot:
            time.sleep(.15)
            command('screendump',{'filename':str(OUT/screenshot)})
            # Leave this screenshot's screen without writing settings.
            key('esc')
    return actions


def record(path):
    data=path.read_bytes()
    assert len(data)==64
    return struct.unpack('<Q14I',data)


if __name__=='__main__':
    image=efi.disk('setup-esp',(OUT/'BOOTAXP64.EFI').read_bytes())
    path=OUT/'setup-test-settings.bin'
    path.unlink(missing_ok=True)
    # Capture the BIOS-style Main page, then leave without making a store.
    ui('f10-main',efi.nvme(image),setup=True,settings_file=path,
       actions=navigate((),screenshot='firmware-setup.ppm'))
    assert not path.exists(),'Discard wrote settings'
    # Boot tab -> startup delay -> 5 seconds -> save.
    ui('f10-save',efi.nvme(image),setup=True,settings_file=path,
       actions=navigate(('right','down','ret','f10')),required=('Firmware settings saved',))
    assert record(path)[3]==5
    saved=path.read_bytes()
    efi.run('setup-restart', ['-M','kotl-ev9,settings-file='+str(path),*efi.nvme(image)],
            required=('Firmware settings: loaded saved configuration','setup (5 seconds)',
                      'ALPHA EFI SELFTEST COMPLETE'))
    ui('f10-discard',efi.nvme(image),setup=True,settings_file=path,
       actions=navigate(('right','down','ret','esc')),required=('Firmware settings discarded',))
    assert path.read_bytes()==saved
    ui('f10-serial',efi.nvme(image),setup=True,serial_f12=True,settings_file=path,
       actions=navigate(('f9','esc')),required=('Firmware settings discarded',))
    assert path.read_bytes()==saved
    ui('f10-defaults-save',efi.nvme(image),setup=True,settings_file=path,
       actions=navigate(('f9','f10')),required=('Firmware settings saved',))
    assert record(path)[3:5]==(2,1)
    # Default loader selection survives a new process. The first ESP returns
    # from hello; the second runs the terminating self-test.
    two=multi_esp('setup-two-esps',(OUT/'HELLOAXP64.EFI').read_bytes(),
                  (OUT/'BOOTAXP64.EFI').read_bytes())
    ui('f10-preferred',efi.nvme(two),setup=True,settings_file=path,
       actions=navigate(('right','ret','f10')),required=('Firmware settings saved',))
    assert record(path)[7]==1
    preferred=path.read_bytes()
    efi.run('preferred-restart',['-M','kotl-ev9,settings-file='+str(path),*efi.nvme(two)],
            required=('ALPHA EFI SELFTEST COMPLETE',))
    output=ui('f12-one-time',efi.nvme(two),settings_file=path,keys=('up',),complete=False,
              required=('Hello from Alpha EFI!',))
    assert 'ALPHA EFI SELFTEST COMPLETE' not in output and path.read_bytes()==preferred
    # Disabling graphical text preserves serial and the EFI GOP driver.
    ui('f10-console',efi.nvme(image),setup=True,settings_file=path,
       actions=navigate(('right','right','ret','f10')),
       required=('Graphical text console: disabled; serial remains active',))
    assert record(path)[4]==0
    # Host write failure must stay in setup and must not apply pending values.
    missing=OUT/'missing-settings-parent'/'settings.bin'
    assert not missing.parent.exists()
    ui('f10-save-failure',efi.nvme(image),setup=True,settings_file=missing,
       actions=navigate(('right','down','ret','f10','esc'),failed_save=True),
       required=('Firmware settings save failed','Firmware settings discarded'))
    assert not missing.exists()
    corrupt=OUT/'corrupt-settings.bin'
    damaged=bytearray(preferred);damaged[16]^=1;corrupt.write_bytes(damaged)
    efi.run('corrupt-settings',['-M','kotl-ev9,settings-file='+str(corrupt),*efi.nvme(image)],
            required=('Firmware settings: defaults (empty or invalid store)',
                      'setup (2 seconds)','ALPHA EFI SELFTEST COMPLETE'))
    assert corrupt.read_bytes()==damaged,'Boot silently overwrote corrupt store'
    def enter_from_boot(key,read_until,command,data,proc):
        key('f10');read_until(b'firmware-setup> ');key('esc')
    ui('f12-to-f10',efi.nvme(image),settings_file=path,actions=enter_from_boot,
       required=('FIRMWARE SETUP (F10)',))
    ui('f10-rescan',efi.nvme(image),setup=True,settings_file=path,
       actions=navigate(('right','down','down','ret','esc')),
       required=('Storage scan complete.',))
    # A valid checksum cannot make an out-of-range timeout acceptable.
    invalid=bytearray(preferred)
    struct.pack_into('<I',invalid,16,0)
    struct.pack_into('<I',invalid,36,0)
    struct.pack_into('<I',invalid,36,zlib.crc32(invalid))
    invalid_path=OUT/'invalid-range-settings.bin';invalid_path.write_bytes(invalid)
    efi.run('invalid-range-settings',['-M','kotl-ev9,settings-file='+str(invalid_path),*efi.nvme(image)],
            required=('Firmware settings: defaults (empty or invalid store)',
                      'setup (2 seconds)','ALPHA EFI SELFTEST COMPLETE'))
    ui('f10-exit-tab',efi.nvme(image),setup=True,settings_file=path,
       actions=navigate(('left','down','ret'),final_enter_exits=True),
       required=('Discard Changes and Boot','Firmware settings discarded'))
    print('All 16 BIOS setup and persistence scenarios passed.',flush=True)
