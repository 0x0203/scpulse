
## SC Pulse Demo

This is a proof-of-concept demonstration for an engine management idea I had for the game Star Citizen. It is an audio driven simulation where the parameters of the audio being played change the resulting sound of the engine, which in turn change how the power is managed for the simulated ship systems. 

A playable demo can be found at [https://0x0203.github.io/scpulse/scpulse_web.html](https://0x0203.github.io/scpulse/scpulse_web.html). Running on a desktop browser is recommended. It is **not** intended to be mobile friendly, so may or may not work on phones/tablets. Given the sound frequencies being used, you will most likely need headphones to hear the audio if running from a mobile platform.

#### Core Ideas

- An experienced player should be able to hear what the engine is doing and know how it's configured from sound alone
- Different engine configurations should be required for optimal engine performance depending on circumstance.
- Through only a few configurable options, a complex simulation can be created that forces players to make tradeoffs between stealth (heat emmisions), power output, resource/fuel consumption, and component health.
- There should be enough challenge to keep players engaged in interesting optimization problems, rather than timeout-driven button pressing make-work. 

#### Caveats:

- This is a proof-of-concept only. There would need to be much more tuning/testing of the specific values that ballance all the various components of this simulation. Please consider the ideas presented, rather than this specific implementation.
- If it takes three tries to write a good program (make it work, make it correct, make it good), this code base is squarely in very first stage. It's been cobbled together over several weeks when I had spare minutes or hours. No design or planning went into it, and was largely an excuse to learn a bit more about [Raylib](https://www.raylib.com/)/[Raygui](https://github.com/raysan5/raygui), [MiniAudio](https://miniaud.io/), and [emscripten](https://emscripten.org/), so this project is mostly a scratchpad of experimentation with those projects. The code quality is therefore **entirely unsuitable** for anything other than playing around/experimentation. 
- There is much that I didn't model in this simulation, and would be much room from improvment/enhancement. Again, this is just a concept exploration. 

#### Details

##### Engine and Audio

- The engine operates on a base frequency of 40Hz. The more input power is supplied, the more output power is generated, but the fuel consumption rate grows exponentially with input power.
    - There is a natural refueling rate of 50 L/s
- In order to consistently get more output power, the Q, R, and S rings need to utilized. Each has a frequency range that is slightly off from the 40Hz base sound wave. As these sound waves are combined, they constructively and destructively interfere such that the total power output will be sometimes above and sometimes below the input power level. 
- The more power applied from each ring, the more heat is generated. The S ring adds less heat to the system at full power than does the R ring, which adds less than the Q ring. 
    - Input power has little effect on heat output on its own, but has a multiplying effect on the heat output of the ring's power.
- Over powering the engine causes damage. 
- Overheating damages the engine.

##### Power Taps

- There are four power taps coming from the engine power output. The botom 10% of power generated always goes directly to battery. The remaining 90% is split into three taps of 30% of total power output each.
- Each power tap can be directed to thrusters, shields, or weapons.
- If multiple taps are routed to the same power drain, the power usage is split between them.
- The last power tap is more efficient at converting power into capacitor charge than the middle tap. The middle tap is more efficient than the low tap. The low tap is more efficient than the battery tap.
- Each power tap can have only a single capacitor configured at a time.

##### Capacitors

- Capacitors are what the power drains ultimately draw their power from.
- If a capacitor has no charge and something tries to draw power from that capacitor, the power will be drawn from the battery instead.
- Capacitors become damaged if they are overcharged beyond their over-charge window.
- If a capacitor's charge is full, it will continue to charge as long as there is power being applied from its power tap, relative to the amount of power being applied.
- If a capacitor's charge is over full, it will naturally decay until at full, as long as there is no charge being applied from the power tap.
- A capacitor in overcharge will generate additional heat.
- Capacitors come in sizes small, medium and large.
    - Small capacitors hold less charge and have a smaller over-charge window than medium. And medium less than large.
- Capacitors come in grades consumer, professional, and military.
    - A military grade capacitor takes damage more slowly than professional, and professional less than consumer.
    - A military grade capacitor has a larger over-charge window than professional, and professional larger than consumer.
    - A military grade capacitor will decay down to full charge (from over-charge or within the over-charge window) more slowly than professional, and professional less than consumer.
    - A military grade capacitor will generate less heat in overcharge than professional, and professional less than consumer.
- The optimal place to keep a capacitor that is actively supplying power to a drain is within the over-charge window, as capacitors over-charge more rapidly within this window and therefore have more power to supply the drain. However, overcharging will cause damage, so it can be a difficult balance.
- The more damaged a capacitor is, the more slowly it will charge.

##### Battery

- If no tap is directed to a power drain in use, that power will be drawn from the battery.
- Since batteries are much less effecient at supplying high-current demands, battery usage results in much heat being generated.
- If there is no charge in available capacitors, and the battery charge is zero, then the power drain will be disabled.

##### Power Drains
- Simulated power drains can be enabled individually for thrusters, shields and weapons. 
- These drains are a very basic example of possible usage patterns for the various devices. Click the "Randomize" button for a new random usage pattern for all power drains.

### Building

You will need the static libs for raylib for your platform. Point to them using the SLIBS_LINUX and SLIBS_WEB variables in the makefile 

This project was only compiled for linux desktop (Xorg only; no wayland) and for the web using emscripten. If building the web target, you will need to have the emsdk enviroment configuration script located as shown in the make target 'web'. 

### Licence

The unique portions of this project (scpulse.c and the makefile) are provided with no restrictions. Do with them as you wish. 

Raylib, Raygui and the custom vertical slider gui controls come directly from their respective projects and are licensed accordingly (an unmodified zlib/libpng license). See their project pages linked above for details.

Miniaudio is licenced according to it's original license (public domain or MIT No Attribution). See their project page linked above for details.
